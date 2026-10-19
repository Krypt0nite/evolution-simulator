# Simulation model

This document specifies exactly what the engine computes. Every formula here is
implemented in `src/` and every constant is a config key (defaults in
`config/default.json`). Units: distance in *world units*, time in *ticks*
(fixed `dt = 1`), energy in *energy units* (plant biomass and stored energy
share the same unit).

## 1. World

A bounded rectangle (`world.width × world.height`, default 600 × 400). Organisms
that would cross an edge are reflected back and their heading component is
flipped. There are no obstacles; *hazards* (below) play the role of dangerous
regions.

## 2. Energy: where it enters, moves and leaves

```
             regrowth                 grazing × efficiency           predation × meat efficiency
  (sun) ───────────────▶  PLANTS  ──────────────────────▶  PREY  ──────────────────────────────▶  PREDATORS
                            │                               │  │                                     │
                            │ decay, depletion              │  │ maintenance, movement, thermal,     │ same
                            ▼                               ▼  ▼ hazards, reproduction overhead,     ▼
                          (lost)                 digestion loss   carcass at non-predation death   (lost)
```

* **The only natural source** is plant regrowth. Organisms added by the config
  or by events bring their initial energy with them (`introduced`).
* **Every loss is recorded** in an `EnergyLedger` with 11 channels. The test
  suite checks, for every scenario and repeatedly during each run, that

  `food + Σ organism energy == initial + inflow + introduced − Σ losses`

  to a relative tolerance of 1e‑9 (`tests/test_simulation.cpp`).
* Costs can never overdraw an organism: each cost is charged as
  `min(cost, energy)`; an organism whose energy reaches zero dies of starvation
  at the end of the metabolism phase.
* Carcasses are *not* modelled as food: energy left in a body at a
  non-predation death leaves the system (`carcass`). This is a deliberate
  simplification (no scavengers/decomposers) and means energy cannot recycle
  into plants.

## 3. Plants (resource field)

The world is tiled into cells of `resources.cellSize` (default 5). Each cell
holds biomass `b` and has a capacity

`K(cell, t) = capacity × profile(cell) × season(t) × multiplier`

* `profile = 1` for `uniform`; for `clustered`/`shifting`,
  `profile = background + (1 − background) · max_p exp(−d_p² / 2σ²)` with
  `σ = patchRadius / 2` over randomly placed patch centres.
* `shifting`: the patch centres are re-drawn every `shiftPeriod` ticks.
* `season(t) = 1 + seasonAmplitude · sin(2πt / seasonPeriod)`.
* `multiplier` is changed by `foodMultiplier` events.

Per tick, for `b < K`: `growth = r·b·(1 − b/K) + s·K·(1 − b/K)`
(logistic growth plus a small spontaneous seeding term `s = seedRate` so emptied
cells can recover), capped so `b ≤ K`. For `b > K` (after K was lowered), 5% of
the excess decays per tick. Growth is the ledger's `inflow`; decay is `decay`.

Food is finite: grazed cells regrow at a rate bounded by `r` and `K`, so
survival is always limited by supply.

## 4. Organisms

State: id, kind (prey or predator), genome, expressed phenotype, position,
heading, energy, age, reproduction cooldown, predator handling timer, offspring
count, parents, founder (lineage root), generation, genetic cluster, last action
and the last 8 actions.

### Phenotype expression (`src/organisms/Phenotype.cpp`)

| Quantity | Formula |
|---|---|
| max speed | `speed` gene |
| vision radius | `vision` gene |
| energy capacity | `energyPerSize · size` (× `storageMultiplier` for predators) |
| bite per tick | `biteRate · size · √metabolism` |
| assimilation | `efficiency` gene (prey energy: × `meatAssimilation`, capped at 0.95) |
| maintenance per tick | `basalRate · size^0.75 · metabolism · (0.5 + efficiency) · (1 + lifespanCost·lifespan/6000) + visionCost · vision` (× `basalMultiplier` for predators) |
| movement cost | `moveCost · size^0.7 · d²` for a step of length `d` |
| thermal cost | `thermalCost · size^0.75 · (T − thermalOptimum)²` |
| breeding threshold | `thresholdFrac · energy capacity` |

These encode the trade-offs evolution works with: everything useful costs
something. Vision costs maintenance; a better gut (efficiency) costs
maintenance; a faster metabolism feeds faster but burns more; a long-lived soma
is expensive; larger bodies store more, bite more and resist predators but pay
more in absolute terms. Maintenance scales as size^0.75 (Kleiber's law) and the
cost of transport as size^0.7 (sublinear, so per unit mass small bodies pay more
to move).

### Senescence

Each tick an organism dies of old age with probability
`min(1, senescenceRate · exp((age − lifespan) / (0.1 · lifespan)))`: negligible
early in life, 2% per tick at `age = lifespan`, rising steeply after.

## 5. Behaviour

### Perception (`Simulation::perceive`)

Organisms only know what is inside their vision radius:

* **Food (prey):** the vision disc is sampled on a stride of
  `max(1, ⌈vision/cell⌉ / 5)` cells. The best cell maximises
  `biomass / (1 + distance / (4·maxSpeed))`; cells under 5% of capacity are
  ignored.
* **Other organisms:** an exact spatial-grid query within the vision radius,
  evaluating at most `attentionLimit` (24) neighbours. It reports the nearest
  predator (for prey), nearest prey (for predators), the nearest compatible
  ready mate (sexual mode), the count and centroid of conspecifics.
* **Hazards:** the nearest hazard edge within vision, as a proximity in [0, 1].
* **Internal state:** hunger `1 − energy/capacity`, age as a fraction of
  lifespan, readiness to breed, temperature mismatch at its position.

### Controllers (`src/organisms/Brain.cpp`)

The controller is chosen by `controller.type`; both see the same perception.

**Utility controller (default).** Scores each available action and picks the
highest (ties keep the earlier one in this order):

| Action | Score |
|---|---|
| rest | `wRest · (1 − h) · 0.8` |
| forage (prey, food seen) | `wForage · (0.3 + h) · (0.5 + 0.5·food amount)` |
| hunt (predator, prey seen) | `wForage · (0.3 + h) · 1.2` |
| explore | `wExplore · (0.25 + 0.75h) · (0.3 if a target is visible else 1)` |
| flee (predator seen) | `wFlee · (1 − riskTolerance) · (1.5 − d/vision) · 1.5` |
| avoid hazard | `wFlee · (1 − riskTolerance) · proximity · 1.5` |
| seek mate (sexual, ready, mate seen) | `wMate · (0.5 + age/lifespan)` |
| social (conspecifics seen) | `|social| · 0.4`, toward the group if `social > 0`, away if `< 0` |

The weights `wForage, wExplore, wRest, wFlee, wMate`, `social` and
`riskTolerance` are genes, so the decision policy itself evolves.

**Neural controller.** A 13-input → `neuralHidden` (8) tanh → 3-output network
whose weights (131 by default) are appended to the genome, initialised as
N(0, 0.7) and mutated like other genes. Inputs: hunger; scaled direction vectors
to food, threat, prey and mate; crowding; temperature mismatch; readiness; bias.
Outputs: a steering direction and a speed fraction. Nothing about foraging is
pre-wired; founders start with random weights.

Adding a controller means implementing `Controller::decide()` and
`genomeExtraSize()`; the engine does not change.

## 6. Interactions and the lifecycle

One tick (see `docs/ARCHITECTURE.md` for the exact phase order):

1. **Environment:** scheduled events fire; the temperature cycle advances;
   shifting patches relocate on schedule.
2. **Sense and decide** from start-of-tick state (order-independent).
3. **Move:** `distance = speedFraction · maxSpeed`; movement cost charged.
4. **Feed (prey):** interference competition. Prey are ordered by
   `size^contestWeight × U(0.5, 1.5)` and each takes
   `min(bite, cell biomass, room left / efficiency)` from its cell. Larger
   organisms tend to win a contested cell (`contestWeight = 0` makes it a pure
   scramble).
5. **Predation:** a hungry predator (energy < 90%) not handling a previous kill
   attacks the nearest prey within `contactRadius·(size_a + size_b) + strikeRange`.
   The attempt costs `huntCost` and succeeds with probability

   `captureBase · speedFactor · sizeFactor / (groupFactor · interferenceFactor)`

   * `speedFactor = 2ρ/(1+ρ)`, `ρ = clamp(v_pred / v_prey, 0.25, 2)`
   * `sizeFactor = 1 / (1 + sizeDefense · max(0, size_prey/size_pred − 0.5))`
   * `groupFactor = 1 + groupDefense · (prey within 6 units) · max(0, social_prey)`
     (group vigilance/defence for social prey)
   * `interferenceFactor = 1 + interference · (other predators within 15 units)`

   On success the prey dies and the predator assimilates up to
   `prey energy × meat efficiency`, then spends `handlingTime` ticks unable to
   hunt (a type II functional response).
6. **Metabolism and ageing:** maintenance, thermal and hazard costs; age +1;
   starvation and senescence deaths.
7. **Reproduction** (if `reproduction.enabled`), for organisms that are mature,
   off cooldown and above the breeding threshold:
   * *Asexual:* with probability `fertility` per tick, the parent gives
     `investment · energy` to one offspring and loses `overheadFrac` × that on
     top (overhead). Genes are copied, then mutated.
   * *Sexual:* the parent must touch an eligible, unclaimed partner of the same
     kind whose genome distance ≤ `compatibilityThreshold`. With probability
     `(fertility_a + fertility_b)/2` both contribute `investment/2 · energy`
     (+ overhead each); genes come from uniform crossover, then mutation. Each
     organism mates at most once per tick and both enter cooldown.
   * Offspring energy above the child's capacity is lost (`overflow`).
   * Births stop when the population reaches `maxPopulation` (a safety cap,
     default 20,000, far above what the default ecology supports).
8. **Commit:** dead organisms are removed and births appended (ids keep
   increasing, so the population stays sorted by id).
9. **Regrowth.**

Nothing ranks organisms by a fitness score. Survival and reproduction depend
only on the rules above; "fitness" in the analytics is always an *observed*
quantity (offspring, lifespan).

## 7. Environment

* **Temperature:** `T(y, t) = offset + gradient · (2y/H − 1) + A·sin(2πt/P)`.
  The default gradient (±0.6 from top to bottom) creates thermal niches.
  Mismatch with the `thermalOptimum` gene costs energy.
* **Hazards:** discs that drain `damage` energy per tick from anything inside.
  Organisms see them and can avoid them, at a behavioural cost set by
  `riskTolerance` and `wFlee`.
* **Events** (in config or applied live from the browser), each applied at the
  start of its tick:

| type | parameters | effect |
|---|---|---|
| `foodMultiplier` | `value` | scales plant capacity (scarcity or abundance) |
| `setRegrowth` | `value` | changes the logistic growth rate |
| `temperatureShift` | `value` | sets the global temperature offset |
| `addHazard` | `x, y, radius, damage` | adds a hazard disc |
| `clearHazards` | | removes all hazards |
| `shiftResources` | | relocates food patches now |
| `depleteRegion` | `x, y, radius` | removes all biomass in a disc |
| `introducePredators` | `count` | adds founder predators |
| `removeRandom` | `count` | removes random organisms (catastrophe) |

Events applied in the browser are appended to the effective config with their
tick, so saving the config and seed reproduces the interactive run exactly
(tested).

## 8. Simplifying assumptions and what they imply

| Assumption | Implication |
|---|---|
| No carcass recycling or decomposers | Energy only flows downward; nutrient limitation is not modelled. |
| One plant "species" with logistic growth per cell | No plant evolution or plant competition; spatial pattern comes only from the capacity profile. |
| Perception is noise-free inside the vision radius | Organisms never misjudge distance; sensing quality varies only through radius. |
| Fixed attention limit (24 neighbours) | In very dense crowds, distant neighbours inside the radius are ignored (a bounded-rationality assumption, also a performance bound). |
| Movement is instantaneous to the chosen speed | No acceleration or inertia; sprinting costs only through the speed² term. |
| Genes map to traits additively, no dominance or epistasis | Crossover blends trait values; sexual populations cannot hide recessive variation. |
| Predators and prey share one genome layout | Predator traits evolve under the same gene ranges; prey-only genes are inert for predators and vice versa. |
| A predator–prey "species" is a fixed kind | Prey cannot evolve into predators. |
| Hard gene bounds (with reflection) | Selection can push a trait against its bound (e.g. minimum body size under scarcity); such results say "as far as the model allows", not where the real optimum lies. |
| Small worlds (hundreds of organisms) by default | Genetic drift is strong and extinctions are frequent; this is realistic for small populations, so compare across seeds rather than trusting one run. |

## 9. Determinism

For a fixed engine version, config and seed, the trajectory is fully
determined. All randomness comes from one `std::mt19937_64` stream with
hand-written distributions, every state-affecting transcendental function goes
through `src/core/DetMath.hpp` (built only from correctly rounded IEEE-754
operations), and FMA contraction is disabled. As a result the native build
(GCC/glibc, x86-64) and the WebAssembly build (Clang/musl) produce identical
state hashes tick for tick (`scripts/compare_native_wasm.mjs`). Other compilers
and CPUs are expected to match too, but only the pairs above have been
verified; see the README.
