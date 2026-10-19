# Genetics

## Genome representation

A genome is a flat vector of real numbers (`src/organisms/Genome.hpp`):

* 18 named **trait genes**, each with a hard valid range and a founder range;
* for the neural controller only, the network **weights** are appended
  (131 by default, range ±4).

The genotype (genes) and phenotype (expressed capabilities and costs) are kept
separate: `express(genome, kind, config)` computes the phenotype, and the same
genome can express differently if the config changes (for example the vision
cost).

| Gene | Range | Founders | Effect |
|---|---|---|---|
| speed | 0.2 – 4.0 | 0.8 – 1.6 | max distance per tick; movement cost ∝ size^0.7 · v² |
| vision | 2 – 60 | 10 – 25 | perception radius; maintenance ∝ radius |
| metabolism | 0.3 – 3.0 | 0.8 – 1.2 | bite rate ∝ √m; maintenance ∝ m |
| size | 0.5 – 3.0 | 0.9 – 1.3 | storage, bite, predator defence; maintenance ∝ size^0.75 |
| efficiency | 0.2 – 0.95 | 0.4 – 0.6 | assimilated fraction of food; maintenance ∝ (0.5 + e) |
| fertility | 0.02 – 1.0 | 0.1 – 0.3 | per-tick chance to breed when eligible |
| maturity | 10 – 800 | 80 – 160 | age at which breeding becomes possible |
| lifespan | 100 – 6000 | 800 – 1500 | onset of senescent mortality; maintenance ∝ lifespan |
| riskTolerance | 0 – 1 | 0.3 – 0.7 | 0 = flees readily, 1 = ignores danger |
| social | −1 – 1 | −0.2 – 0.2 | attraction to (+) or avoidance of (−) conspecifics; group defence |
| mutationRate | 0.001 – 0.3 | 0.03 – 0.08 | per-gene mutation probability passed to offspring |
| investment | 0.1 – 0.7 | 0.3 – 0.45 | share of parent energy given to each offspring |
| thermalOptimum | −1.5 – 1.5 | −0.2 – 0.2 | preferred temperature |
| wForage, wExplore, wRest, wFlee, wMate | 0 – 2 | see `evosim genes` | utility weights of the decision policy |

`evosim genes` prints this table from the engine itself.

## Inheritance

* **Asexual:** the offspring genome is a copy of the parent's, then mutated.
* **Sexual:** uniform crossover (each gene independently from either parent with
  probability ½), then mutation. Neural weights cross over the same way.
  Mating requires genome distance ≤ `reproduction.compatibilityThreshold`, a
  simple pre-zygotic isolation rule.

## Mutation (`mutate()`)

For every gene independently:

1. With probability `p` the gene mutates, where `p` is the parent's own
   `mutationRate` gene (`mutation.mode = "evolvable"`, so the mutation rate itself
   evolves) or `mutation.fixedRate` (`"fixed"`, which also freezes the
   mutationRate gene).
2. The change is Gaussian with mean 0. Its standard deviation is
   `mutation.scale × gene range` (default 4% of the range) or, with probability
   `mutation.largeProb` (5%), `mutation.largeScale × gene range` (30%): rare
   large-effect mutations.
3. The result is folded back into the valid range by **reflection**, which
   keeps every value valid without piling probability mass up at the bounds the
   way clamping does.

Mutations are unbiased (tested: mean change 0, spread equal to the configured
scale), so whether a mutation helps, hurts or does nothing is decided entirely
by its ecological consequences. Genes listed in `population.lockedGenes` never
mutate, which lets an experiment hold one trait fixed while everything else
evolves.

## Distance and diversity

* **Genome distance** between two organisms: Euclidean distance over the 18 trait
  genes, each scaled by its range, divided by √18. 0 means identical; 1 means
  opposite corners of trait space. Neural weights are excluded (they are not
  comparable between unrelated networks).
* **Genetic diversity** (statistics): the mean pairwise genome distance over an
  evenly strided sample of up to 160 living prey. The sample is chosen without
  random numbers so measuring diversity never changes the simulation.
* **Per-trait spread:** the population SD of every gene is recorded each sample.

## Lineages

Every organism stores its parents, its generation (`max(parent generations) + 1`)
and its **founder**, the generation-0 ancestor reached by following the first
parent. The lineage tracker keeps a record of every organism ever born
(birth/death tick, cause of death, offspring count), so the inspector can walk
ancestors, list children and count all descendants of any organism, alive or
dead. The browser colours organisms by founder and plots each founder's share
of the living population over time, which makes selective sweeps visible.

## Genetic clusters (speciation module)

Every `analytics.clusterInterval` ticks the population is grouped by leader
clustering on genome distance (`clusterThreshold`, default 0.2; prey and
predators separately). New clusters are matched to the previous pass by centroid
so cluster ids (and colours) stay stable over time.

Clusters are a descriptive measure of divergence. They do **not** demonstrate
speciation: real species boundaries require reproductive isolation. The
`thermal_niches` preset combines a strong temperature gradient with sexual
reproduction and a tighter compatibility threshold, so genetic divergence can
also reduce interbreeding, a step toward, but still not proof of, isolation.
