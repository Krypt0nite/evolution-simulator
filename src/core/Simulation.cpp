#include "core/DetMath.hpp"
#include "core/Simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace evo {

namespace {
constexpr double kGridCell = 12.0;
constexpr double kGroupRadius = 6.0;          // prey within this distance count as a defensive group
constexpr double kInterferenceRadius = 15.0;  // predators within this distance interfere
}  // namespace

Simulation::Simulation(SimulationConfig cfg) : cfg_(std::move(cfg)), rng_(cfg_.seed) {
    auto problems = cfg_.validate();
    if (!problems.empty()) {
        std::string msg = "invalid configuration:";
        for (auto& p : problems) msg += "\n  - " + p;
        throw ConfigError(msg);
    }
    controller_ = makeController(cfg_.controller.type, cfg_.controller.neuralHidden, cfg_.reproduction.mode == "sexual");
    for (const auto& g : cfg_.population.lockedGenes) locked_[static_cast<std::size_t>(geneIndex(g))] = true;
    events_ = cfg_.environment.events;
    std::stable_sort(events_.begin(), events_.end(), [](const EnvEvent& a, const EnvEvent& b) { return a.tick < b.tick; });

    food_.configure(cfg_, rng_);
    env_.configure(cfg_);
    grid_.configure(cfg_.world.width, cfg_.world.height, kGridCell);
    lineage_.configure(static_cast<std::size_t>(cfg_.analytics.maxLineageRecords));
    species_.configure(cfg_.analytics.clusterThreshold);
    stats_.configure(cfg_.analytics.maxSamples);
    maxBodySize_ = geneSpec(BodySize).max;

    // Founders.
    const auto& pop = cfg_.population;
    if (pop.groups.empty()) {
        spawnFounders(Kind::Prey, pop.initialPrey, 0);
    } else {
        double total = 0;
        for (const auto& g : pop.groups) total += g.fraction;
        int remaining = pop.initialPrey;
        for (std::size_t k = 0; k < pop.groups.size(); ++k) {
            const auto& grp = pop.groups[k];
            int n = k + 1 == pop.groups.size() ? remaining
                                               : static_cast<int>(std::lround(pop.initialPrey * grp.fraction / total));
            n = std::min(n, remaining);
            remaining -= n;
            auto merged = pop.initialTraits;
            for (const auto& [gene, r] : grp.traits) merged[gene] = r;
            const InitRanges ranges = initRangesFor(merged);
            for (int i = 0; i < n; ++i) {
                Organism o;
                o.genome = founderGenome(ranges);
                o.kind = Kind::Prey;
                o.id = nextId_++;
                o.founder = o.id;
                o.pheno = express(o.genome, o.kind, cfg_);
                o.pos = {rng_.uniform(0.0, cfg_.world.width), rng_.uniform(0.0, cfg_.world.height)};
                o.heading = jitterDirection({1.0, 0.0}, 3.0, rng_);
                o.energy = pop.initialEnergyFrac * o.pheno.maxEnergy;
                o.age = static_cast<std::uint32_t>(rng_.uniform(0.0, 0.5 * o.pheno.maturity));
                lineage_.recordBirth(o, 0);
                organisms_.push_back(std::move(o));
            }
        }
    }
    if (cfg_.predators.enabled && cfg_.predators.introduceAtTick == 0)
        spawnFounders(Kind::Predator, cfg_.predators.initialCount, 0);

    ledger_ = EnergyLedger{};
    ledger_.initial = food_.total() + organismEnergy();
    species_.update(organisms_);
    sample();
}

InitRanges Simulation::initRangesFor(const std::map<std::string, std::pair<double, double>>& overrides) const {
    InitRanges r = defaultInitRanges();
    for (const auto& [gene, range] : overrides) r[static_cast<std::size_t>(geneIndex(gene))] = {range.first, range.second};
    return r;
}

Genome Simulation::founderGenome(const InitRanges& ranges) {
    return randomGenome(rng_, ranges, controller_->genomeExtraSize());
}

void Simulation::spawnFounders(Kind kind, int count, std::uint64_t forTick) {
    gridValid_ = false;
    auto overrides = cfg_.population.initialTraits;
    if (kind == Kind::Predator)
        for (const auto& [gene, r] : cfg_.predators.initialTraits) overrides[gene] = r;
    const InitRanges ranges = initRangesFor(overrides);
    for (int i = 0; i < count; ++i) {
        if (static_cast<int>(organisms_.size()) >= cfg_.population.maxPopulation) break;
        Organism o;
        o.genome = founderGenome(ranges);
        o.kind = kind;
        o.id = nextId_++;
        o.founder = o.id;
        o.pheno = express(o.genome, kind, cfg_);
        o.pos = {rng_.uniform(0.0, cfg_.world.width), rng_.uniform(0.0, cfg_.world.height)};
        o.heading = jitterDirection({1.0, 0.0}, 3.0, rng_);
        o.energy = cfg_.population.initialEnergyFrac * o.pheno.maxEnergy;
        o.age = static_cast<std::uint32_t>(rng_.uniform(0.0, 0.5 * o.pheno.maturity));
        ledger_.introduced += o.energy;
        lineage_.recordBirth(o, forTick);
        organisms_.push_back(std::move(o));
    }
}

const Organism* Simulation::find(std::uint64_t id) const {
    auto it = std::lower_bound(organisms_.begin(), organisms_.end(), id,
                               [](const Organism& o, std::uint64_t v) { return o.id < v; });
    return it != organisms_.end() && it->id == id ? &*it : nullptr;
}

int Simulation::preyCount() const {
    int n = 0;
    for (const auto& o : organisms_) n += o.alive && o.kind == Kind::Prey;
    return n;
}

int Simulation::predatorCount() const {
    int n = 0;
    for (const auto& o : organisms_) n += o.alive && o.kind == Kind::Predator;
    return n;
}

double Simulation::organismEnergy() const {
    double e = 0;
    for (const auto& o : organisms_) if (o.alive) e += o.energy;
    for (const auto& o : births_) e += o.energy;
    return e;
}

void Simulation::scheduleEvent(EnvEvent e) {
    e.tick = static_cast<int>(tick_);
    auto pos = std::upper_bound(events_.begin() + static_cast<std::ptrdiff_t>(eventCursor_), events_.end(), e.tick,
                                [](int t, const EnvEvent& ev) { return t < ev.tick; });
    events_.insert(pos, e);
    cfg_.environment.events.push_back(e);
}

void Simulation::applyEvent(const EnvEvent& e) {
    if (e.type == "foodMultiplier") food_.setMultiplier(std::max(0.0, e.value));
    else if (e.type == "temperatureShift") env_.setOffset(e.value);
    else if (e.type == "addHazard") env_.hazards().push_back(Hazard{e.pos, e.radius, e.damage});
    else if (e.type == "clearHazards") env_.hazards().clear();
    else if (e.type == "shiftResources") food_.relocatePatches(rng_);
    else if (e.type == "introducePredators") spawnFounders(Kind::Predator, e.count, tick_);
    else if (e.type == "setRegrowth") food_.setRegrowthRate(std::clamp(e.value, 0.0, 1.0));
    else if (e.type == "depleteRegion") ledger_.depleted += food_.depleteDisc(e.pos, e.radius);
    else if (e.type == "removeRandom") {
        std::vector<std::size_t> alive;
        for (std::size_t i = 0; i < organisms_.size(); ++i) if (organisms_[i].alive) alive.push_back(i);
        rng_.shuffle(alive);
        const std::size_t n = std::min<std::size_t>(alive.size(), static_cast<std::size_t>(std::max(0, e.count)));
        for (std::size_t k = 0; k < n; ++k) kill(organisms_[alive[k]], DeathCause::Culled);
    }
}

void Simulation::run(std::uint64_t ticks) {
    for (std::uint64_t i = 0; i < ticks; ++i) step();
}

void Simulation::step() {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    auto mark = t0;
    auto lap = [&](std::size_t phase) {
        if (!profiling_) return;
        const auto now = clock::now();
        profile_.ms[phase] += std::chrono::duration<double, std::milli>(now - mark).count();
        mark = now;
    };

    // 1. Environment and scheduled events.
    env_.update(tick_);
    while (eventCursor_ < events_.size() && events_[eventCursor_].tick <= static_cast<int>(tick_)) {
        applyEvent(events_[eventCursor_]);
        ++eventCursor_;
    }
    // "shifting" resources: patch centres relocate every shiftPeriod ticks.
    if (cfg_.resources.distribution == "shifting" && tick_ > 0 &&
        tick_ % static_cast<std::uint64_t>(cfg_.resources.shiftPeriod) == 0)
        food_.relocatePatches(rng_);
    if (cfg_.predators.enabled && cfg_.predators.introduceAtTick > 0 &&
        static_cast<std::uint64_t>(cfg_.predators.introduceAtTick) == tick_)
        spawnFounders(Kind::Predator, cfg_.predators.initialCount, tick_);
    lap(0);

    // 2-3. Sense and decide from start-of-tick state.
    rebuildGrid();
    lap(1);
    phaseDecide();
    lap(2);
    // 4. Move.
    phaseMove();
    lap(3);
    // 5. Re-index post-move; random permutation for contested interactions.
    rebuildGrid();
    order_.resize(organisms_.size());
    for (std::size_t i = 0; i < order_.size(); ++i) order_[i] = static_cast<std::uint32_t>(i);
    rng_.shuffle(order_);
    lap(1);
    // 6-9.
    phaseFeed();
    lap(4);
    phasePredation();
    lap(5);
    phaseMetabolism();
    lap(6);
    phaseReproduction();
    lap(7);
    // 10. Commit births/deaths.
    phaseCommit();
    lap(8);
    // 11. Regrowth.
    const auto flow = food_.regrow(tick_);
    ledger_.inflow += flow.added;
    ledger_.decay += flow.decayed;
    lap(9);
    // 12. Bookkeeping.
    ++tick_;
    if (tick_ % static_cast<std::uint64_t>(cfg_.analytics.clusterInterval) == 0) species_.update(organisms_);
    lastStepMs_ = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    stepMsAccum_ += lastStepMs_;
    ++stepMsCount_;
    if (tick_ % static_cast<std::uint64_t>(cfg_.analytics.sampleInterval) == 0) sample();
    lap(10);
}

void Simulation::rebuildGrid() const {
    const std::size_t n = organisms_.size();
    positions_.resize(n);
    include_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        positions_[i] = organisms_[i].pos;
        include_[i] = organisms_[i].alive ? 1 : 0;
    }
    grid_.build(positions_, include_);
    gridValid_ = true;
}

Perception Simulation::perceive(std::size_t i) const {
    if (!gridValid_ || positions_.size() != organisms_.size()) rebuildGrid();
    const Organism& o = organisms_[i];
    const auto& ph = o.pheno;
    Perception p;
    p.vision = ph.vision;
    p.hunger = 1.0 - std::clamp(o.energy / ph.maxEnergy, 0.0, 1.0);
    p.ageFrac = ph.lifespan > 0 ? o.age / ph.lifespan : 1.0;
    p.tempMismatch = env_.temperature(o.pos) - o.genome[ThermalOptimum];
    const bool sexual = cfg_.reproduction.mode == "sexual" && cfg_.reproduction.enabled;
    auto ready = [&](const Organism& x) {
        return x.alive && x.mature() && x.cooldown == 0 && x.energy >= x.pheno.reproThreshold;
    };
    p.readyToMate = sexual && ready(o);

    // Food: sample the vision disc on a stride so the cost per organism is bounded.
    if (o.kind == Kind::Prey) {
        const double cs = food_.cellSize();
        const int cr = static_cast<int>(std::ceil(ph.vision / cs));
        const int stride = std::max(1, cr / 5);
        const int cx0 = static_cast<int>(o.pos.x / cs), cy0 = static_cast<int>(o.pos.y / cs);
        const double r2 = ph.vision * ph.vision;
        const double minWorth = 0.05 * food_.maxCapacity();
        double bestScore = 0.0;
        for (int dy = -cr; dy <= cr; dy += stride) {
            const int cy = cy0 + dy;
            if (cy < 0 || cy >= food_.rows()) continue;
            for (int dx = -cr; dx <= cr; dx += stride) {
                const int cx = cx0 + dx;
                if (cx < 0 || cx >= food_.cols()) continue;
                const int c = cy * food_.cols() + cx;
                const double b = food_.biomass(c);
                if (b < minWorth) continue;
                const Vec2 center = (dx == 0 && dy == 0) ? o.pos : food_.cellCenter(c);
                const double d2 = distanceSq(center, o.pos);
                if (d2 > r2) continue;
                const double d = std::sqrt(d2);
                const double score = b / (1.0 + d / std::max(0.5, ph.maxSpeed * 4.0));
                if (score > bestScore) {
                    bestScore = score;
                    p.foodSeen = true;
                    p.foodDist = d;
                    p.foodDir = d > 1e-9 ? (center - o.pos) / d : Vec2{};
                    p.foodAmount = std::min(1.0, b / food_.maxCapacity());
                }
            }
        }
    }

    // Other organisms within vision (exact grid query, attention-capped evaluation).
    double threatD = std::numeric_limits<double>::max(), preyD = threatD, mateD = threatD;
    Vec2 centroid;
    int seen = 0;
    const int limit = cfg_.organisms.attentionLimit;
    grid_.forEachInRadius(o.pos, ph.vision, [&](std::uint32_t j) {
        if (j == i || seen >= limit) return;
        const Organism& other = organisms_[j];
        if (!other.alive) return;
        ++seen;
        const double d = distance(other.pos, o.pos);
        if (other.kind == o.kind) {
            ++p.neighbours;
            centroid += other.pos;
            if (p.readyToMate && ready(other) && d < mateD &&
                genomeDistance(o.genome, other.genome) <= cfg_.reproduction.compatibilityThreshold) {
                mateD = d;
                p.mateSeen = true;
                p.mateDist = d;
                p.mateDir = (other.pos - o.pos).normalized();
            }
        } else if (o.kind == Kind::Prey && d < threatD) {
            threatD = d;
            p.threatSeen = true;
            p.threatDist = d;
            p.threatDir = (other.pos - o.pos).normalized();
        } else if (o.kind == Kind::Predator && d < preyD) {
            preyD = d;
            p.preySeen = true;
            p.preyDist = d;
            p.preyDir = (other.pos - o.pos).normalized();
        }
    });
    if (p.neighbours > 0) p.neighbourDir = (centroid / p.neighbours - o.pos).normalized();

    for (const auto& h : env_.hazards()) {
        const double d = distance(o.pos, h.center);
        const double edge = d - h.radius;
        if (edge > ph.vision) continue;
        const double prox = edge <= 0 ? 1.0 : 1.0 - edge / ph.vision;
        if (prox > p.hazardProximity) {
            p.hazardSeen = true;
            p.hazardProximity = prox;
            p.hazardAway = d > 1e-9 ? (o.pos - h.center) / d : Vec2{1.0, 0.0};
        }
    }
    return p;
}

void Simulation::phaseDecide() {
    intents_.assign(organisms_.size(), Intent{});
    for (std::size_t i = 0; i < organisms_.size(); ++i) {
        Organism& o = organisms_[i];
        if (!o.alive) continue;
        const Perception p = perceive(i);
        intents_[i] = controller_->decide(o, p, rng_);
        o.action = intents_[i].action;
        o.pushRecent(o.action);
    }
}

double Simulation::charge(Organism& o, double cost, double& field) {
    if (cost <= 0.0) return 0.0;
    const double actual = std::min(cost, o.energy);
    o.energy -= actual;
    field += actual;
    return actual;
}

void Simulation::phaseMove() {
    gridValid_ = false;
    const double W = cfg_.world.width, H = cfg_.world.height;
    for (std::size_t i = 0; i < organisms_.size(); ++i) {
        Organism& o = organisms_[i];
        if (!o.alive) continue;
        const Intent& it = intents_[i];
        Vec2 dir = it.dir;
        const double dist = std::clamp(it.speedFrac, 0.0, 1.0) * o.pheno.maxSpeed;
        Vec2 np = o.pos + dir * dist;
        if (np.x < 0.0) { np.x = -np.x; dir.x = -dir.x; }
        if (np.x >= W) { np.x = 2.0 * W - np.x - 1e-9; dir.x = -dir.x; }
        if (np.y < 0.0) { np.y = -np.y; dir.y = -dir.y; }
        if (np.y >= H) { np.y = 2.0 * H - np.y - 1e-9; dir.y = -dir.y; }
        np.x = std::clamp(np.x, 0.0, W - 1e-9);
        np.y = std::clamp(np.y, 0.0, H - 1e-9);
        const double moved = distance(np, o.pos);
        o.pos = np;
        o.lastMoved = moved;
        if (dir.lengthSq() > 1e-12) o.heading = dir.normalized();
        charge(o, o.pheno.moveCoeff * moved * moved, ledger_.movement);
    }
}

void Simulation::phaseFeed() {
    // Size-asymmetric contest: larger organisms tend to feed first on a
    // contested cell. Priority = size^w × U(0.5, 1.5), drawn in permutation order.
    feedOrder_.clear();
    const double w = cfg_.organisms.contestWeight;
    for (std::uint32_t idx : order_) {
        const Organism& o = organisms_[idx];
        if (!o.alive || o.kind != Kind::Prey) continue;
        const double u = rng_.uniform(0.5, 1.5);
        feedOrder_.push_back({w == 0.0 ? u : det::pow(o.pheno.size, w) * u, idx});
    }
    std::stable_sort(feedOrder_.begin(), feedOrder_.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [priority, idx] : feedOrder_) {
        (void)priority;
        Organism& o = organisms_[idx];
        if (!o.alive || o.kind != Kind::Prey) continue;
        const double eff = o.pheno.assimilation;
        const double room = (o.pheno.maxEnergy - o.energy) / eff;
        if (room <= 0.0) continue;
        const double taken = food_.take(food_.cellAt(o.pos), std::min(o.pheno.biteRate, room));
        const double gained = taken * eff;
        o.energy = std::min(o.pheno.maxEnergy, o.energy + gained);
        ledger_.assimilatedPlants += gained;
        ledger_.digestion += taken - gained;
    }
}

void Simulation::phasePredation() {
    if (!cfg_.predators.enabled) return;
    const auto& pc = cfg_.predators;
    const double cr = cfg_.organisms.contactRadius;
    for (std::uint32_t idx : order_) {
        Organism& pred = organisms_[idx];
        if (!pred.alive || pred.kind != Kind::Predator) continue;
        if (pred.handling > 0) continue;                           // still handling the last kill
        if (pred.energy >= 0.9 * pred.pheno.maxEnergy) continue;  // satiated
        int target = -1;
        double bestD = std::numeric_limits<double>::max();
        grid_.forEachInRadius(pred.pos, cr * (pred.pheno.size + maxBodySize_) + pc.strikeRange, [&](std::uint32_t j) {
            const Organism& q = organisms_[j];
            if (!q.alive || q.kind != Kind::Prey) return;
            const double d = distance(q.pos, pred.pos);
            if (d <= cr * (pred.pheno.size + q.pheno.size) + pc.strikeRange && d < bestD) { bestD = d; target = static_cast<int>(j); }
        });
        if (target < 0) continue;
        Organism& prey = organisms_[static_cast<std::size_t>(target)];
        charge(pred, pc.huntCost, ledger_.hunting);

        const double speedRatio = std::clamp(pred.pheno.maxSpeed / std::max(0.05, prey.pheno.maxSpeed), 0.25, 2.0);
        const double speedFactor = 2.0 * speedRatio / (1.0 + speedRatio);
        const double sizeFactor = 1.0 / (1.0 + pc.sizeDefense * std::max(0.0, prey.pheno.size / pred.pheno.size - 0.5));
        int group = 0;
        if (prey.genome[Social] > 0.0) {
            grid_.forEachInRadius(prey.pos, kGroupRadius, [&](std::uint32_t j) {
                if (static_cast<int>(j) != target && organisms_[j].alive && organisms_[j].kind == Kind::Prey) ++group;
            });
        }
        const double groupFactor = 1.0 + pc.groupDefense * group * std::max(0.0, prey.genome[Social]);
        int rivals = 0;
        if (pc.interference > 0.0) {
            grid_.forEachInRadius(pred.pos, kInterferenceRadius, [&](std::uint32_t j) {
                if (j != idx && organisms_[j].alive && organisms_[j].kind == Kind::Predator) ++rivals;
            });
        }
        const double interferenceFactor = 1.0 + pc.interference * rivals;
        const double p = std::clamp(pc.captureBase * speedFactor * sizeFactor / (groupFactor * interferenceFactor), 0.0, 1.0);
        if (!rng_.chance(p)) continue;

        const double e = prey.energy;
        const double meatEff = std::min(0.95, pred.pheno.assimilation * pc.meatAssimilation);
        const double gain = std::min(e * meatEff, pred.pheno.maxEnergy - pred.energy);
        pred.energy += gain;
        ledger_.assimilatedPrey += gain;
        ledger_.digestion += e - gain;
        prey.energy = 0.0;
        pred.handling = static_cast<std::uint32_t>(pc.handlingTime);
        kill(prey, DeathCause::Predation);
    }
}

void Simulation::kill(Organism& o, DeathCause cause) {
    if (!o.alive) return;
    o.alive = false;
    o.cause = cause;
    ledger_.carcass += o.energy;
    o.energy = 0.0;
}

void Simulation::phaseMetabolism() {
    for (auto& o : organisms_) {
        if (!o.alive) continue;
        const double mismatch = env_.temperature(o.pos) - o.genome[ThermalOptimum];
        charge(o, o.pheno.basalCost, ledger_.basal);
        charge(o, o.pheno.thermalCoeff * mismatch * mismatch, ledger_.thermal);
        charge(o, env_.hazardDrain(o.pos), ledger_.hazard);
        ++o.age;
        if (o.cooldown > 0) --o.cooldown;
        if (o.handling > 0) --o.handling;
        if (o.energy <= 1e-9) {
            kill(o, DeathCause::Starvation);
            continue;
        }
        const double ls = std::max(1.0, o.pheno.lifespan);
        const double hazard = std::min(1.0, cfg_.organisms.senescenceRate * det::exp((o.age - ls) / (0.1 * ls)));
        if (rng_.chance(hazard)) kill(o, DeathCause::OldAge);
    }
}

Organism Simulation::makeChild(const Organism& a, const Organism* b, double energy) {
    Organism c;
    c.genome = b ? crossover(a.genome, b->genome, rng_) : a.genome;
    mutate(c.genome, rng_, cfg_.mutation, locked_);
    c.kind = a.kind;
    c.id = nextId_++;
    c.parentA = a.id;
    c.parentB = b ? b->id : 0;
    c.founder = a.founder;
    c.generation = std::max(a.generation, b ? b->generation : 0u) + 1;
    c.pheno = express(c.genome, c.kind, cfg_);
    Vec2 p = a.pos + Vec2{rng_.normal(0.0, 1.0), rng_.normal(0.0, 1.0)};
    p.x = std::clamp(p.x, 0.0, cfg_.world.width - 1e-9);
    p.y = std::clamp(p.y, 0.0, cfg_.world.height - 1e-9);
    c.pos = p;
    c.heading = jitterDirection(a.heading, 1.0, rng_);
    const double kept = std::min(energy, c.pheno.maxEnergy);
    ledger_.overflow += energy - kept;
    c.energy = kept;
    return c;
}

void Simulation::phaseReproduction() {
    const auto& rc = cfg_.reproduction;
    if (!rc.enabled) return;
    claimed_.assign(organisms_.size(), 0);
    int living = 0;
    for (const auto& o : organisms_) living += o.alive;
    const double ov = rc.overheadFrac;
    const double cr = cfg_.organisms.contactRadius;
    const bool sexual = rc.mode == "sexual";

    auto eligible = [&](std::size_t k) {
        const Organism& o = organisms_[k];
        return o.alive && !claimed_[k] && o.mature() && o.cooldown == 0 && o.energy >= o.pheno.reproThreshold;
    };
    // Energy a parent commits: `invest` goes to the child, `invest·ov` is lost.
    auto commit = [&](Organism& parent, double invest) {
        if (invest * (1.0 + ov) > parent.energy) invest = parent.energy / (1.0 + ov);
        parent.energy -= invest * (1.0 + ov);
        ledger_.reproduction += invest * ov;
        parent.cooldown = static_cast<std::uint32_t>(rc.cooldown);
        ++parent.offspring;
        return invest;
    };

    for (std::uint32_t idx : order_) {
        if (!eligible(idx)) continue;
        if (living + static_cast<int>(births_.size()) >= cfg_.population.maxPopulation) {
            stats_.onSuppressedBirth();
            continue;
        }
        Organism& a = organisms_[idx];
        if (!sexual) {
            if (!rng_.chance(a.pheno.fertility)) continue;
            claimed_[idx] = 1;
            const double invest = commit(a, a.pheno.investment * a.energy);
            births_.push_back(makeChild(a, nullptr, invest));
            continue;
        }
        int partner = -1;
        double bestD = std::numeric_limits<double>::max();
        grid_.forEachInRadius(a.pos, cr * (a.pheno.size + maxBodySize_), [&](std::uint32_t j) {
            if (j == idx || !eligible(j)) return;
            const Organism& b = organisms_[j];
            if (b.kind != a.kind) return;
            const double d = distance(a.pos, b.pos);
            if (d > cr * (a.pheno.size + b.pheno.size) || d >= bestD) return;
            if (genomeDistance(a.genome, b.genome) > rc.compatibilityThreshold) return;
            bestD = d;
            partner = static_cast<int>(j);
        });
        if (partner < 0) continue;
        Organism& b = organisms_[static_cast<std::size_t>(partner)];
        if (!rng_.chance(0.5 * (a.pheno.fertility + b.pheno.fertility))) continue;
        claimed_[idx] = 1;
        claimed_[static_cast<std::size_t>(partner)] = 1;
        const double ia = commit(a, 0.5 * a.pheno.investment * a.energy);
        const double ib = commit(b, 0.5 * b.pheno.investment * b.energy);
        births_.push_back(makeChild(a, &b, ia + ib));
    }
}

void Simulation::phaseCommit() {
    gridValid_ = false;
    std::size_t w = 0;
    for (std::size_t r = 0; r < organisms_.size(); ++r) {
        Organism& o = organisms_[r];
        if (!o.alive) {
            lineage_.recordDeath(o.id, tick_, o.cause);
            stats_.onDeath(static_cast<int>(o.cause), o.age, o.offspring);
            continue;
        }
        if (w != r) organisms_[w] = std::move(o);
        ++w;
    }
    organisms_.resize(w);
    for (auto& c : births_) {
        lineage_.recordBirth(c, tick_);
        stats_.onBirth();
        organisms_.push_back(std::move(c));
    }
    births_.clear();
}

Sample Simulation::makeSample() const {
    Sample s;
    s.tick = tick_;
    std::array<double, kNumTraits> sum{}, sumSq{}, psum{};
    int prey = 0, pred = 0;
    double energyFrac = 0, age = 0, gen = 0;
    std::vector<std::uint64_t> founders;
    founders.reserve(organisms_.size());
    for (const auto& o : organisms_) {
        if (!o.alive) continue;
        energyFrac += o.energy / o.pheno.maxEnergy;
        age += o.age;
        gen += o.generation;
        s.maxGeneration = std::max(s.maxGeneration, o.generation);
        founders.push_back(o.founder);
        if (o.kind == Kind::Prey) {
            ++prey;
            for (int g = 0; g < kNumTraits; ++g) {
                sum[static_cast<std::size_t>(g)] += o.genome[g];
                sumSq[static_cast<std::size_t>(g)] += o.genome[g] * o.genome[g];
            }
        } else {
            ++pred;
            for (int g = 0; g < kNumTraits; ++g) psum[static_cast<std::size_t>(g)] += o.genome[g];
        }
    }
    s.prey = prey;
    s.predators = pred;
    s.population = prey + pred;
    if (s.population > 0) {
        s.meanEnergyFrac = energyFrac / s.population;
        s.meanAge = age / s.population;
        s.meanGeneration = gen / s.population;
    }
    for (int g = 0; g < kNumTraits; ++g) {
        const auto k = static_cast<std::size_t>(g);
        if (prey > 0) {
            s.traitMean[k] = sum[k] / prey;
            s.traitSd[k] = std::sqrt(std::max(0.0, sumSq[k] / prey - s.traitMean[k] * s.traitMean[k]));
        }
        if (pred > 0) s.predTraitMean[k] = psum[k] / pred;
    }
    // Diversity: mean pairwise distance over an evenly-strided prey sample (no RNG use).
    std::vector<const Genome*> sampleG;
    if (prey > 1) {
        const int want = std::min(prey, cfg_.analytics.diversitySample);
        const double step = static_cast<double>(prey) / want;
        int k = 0, taken = 0;
        double next = 0.0;
        for (const auto& o : organisms_) {
            if (!o.alive || o.kind != Kind::Prey) continue;
            if (k >= static_cast<int>(next) && taken < want) { sampleG.push_back(&o.genome); ++taken; next += step; }
            ++k;
        }
        double tot = 0;
        std::size_t pairs = 0;
        for (std::size_t a = 0; a < sampleG.size(); ++a)
            for (std::size_t b = a + 1; b < sampleG.size(); ++b) { tot += genomeDistance(*sampleG[a], *sampleG[b]); ++pairs; }
        s.diversity = pairs ? tot / pairs : 0.0;
    }
    s.foodTotal = food_.total();
    const double cap = food_.totalCapacity();
    s.foodFraction = cap > 0 ? s.foodTotal / cap : 0.0;
    s.temperatureOffset = env_.offset() + env_.cycleValue();
    s.clusters = static_cast<int>(species_.clusters().size());
    std::sort(founders.begin(), founders.end());
    s.lineages = static_cast<int>(std::unique(founders.begin(), founders.end()) - founders.begin());
    s.stepMs = stepMsCount_ ? stepMsAccum_ / stepMsCount_ : 0.0;
    return s;
}

void Simulation::sample() {
    stats_.push(makeSample());
    stepMsAccum_ = 0;
    stepMsCount_ = 0;
}

Json Simulation::debugDump() const {
    auto hex = [](double d) {
        std::uint64_t b;
        std::memcpy(&b, &d, sizeof b);
        char buf[20];
        std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(b));
        return Json(std::string(buf));
    };
    Json j = Json::object();
    j["tick"] = Json(tick_);
    j["rng"] = Json(rng_.fingerprint());
    j["spare"] = hex(rng_.spare());
    Json orgs = Json::array();
    for (const auto& o : organisms_) {
        Json oj = Json::object();
        oj["id"] = Json(o.id);
        oj["x"] = hex(o.pos.x);
        oj["y"] = hex(o.pos.y);
        oj["e"] = hex(o.energy);
        oj["age"] = Json(static_cast<std::uint64_t>(o.age));
        Json g = Json::array();
        for (double v : o.genome.genes) g.push(hex(v));
        oj["g"] = g;
        orgs.push(oj);
    }
    j["organisms"] = orgs;
    Json food = Json::array();
    for (double b : food_.biomassData()) food.push(hex(b));
    j["food"] = food;
    return j;
}

std::uint64_t Simulation::stateHash() const {
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const void* data, std::size_t n) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    };
    auto mixD = [&](double d) { mix(&d, sizeof d); };
    mix(&tick_, sizeof tick_);
    mix(&nextId_, sizeof nextId_);
    const std::uint64_t rf = rng_.fingerprint();
    mix(&rf, sizeof rf);
    const std::uint8_t spareFlag = rng_.hasSpare() ? 1 : 0;
    mix(&spareFlag, 1);
    mixD(rng_.spare());
    for (const auto& o : organisms_) {
        mix(&o.id, sizeof o.id);
        const auto kind = static_cast<std::uint8_t>(o.kind);
        mix(&kind, 1);
        mixD(o.pos.x); mixD(o.pos.y); mixD(o.energy);
        mix(&o.age, sizeof o.age);
        mix(&o.cooldown, sizeof o.cooldown);
        mix(&o.handling, sizeof o.handling);
        for (double g : o.genome.genes) mixD(g);
    }
    for (double b : food_.biomassData()) mixD(b);
    mixD(env_.offset());
    mixD(food_.multiplier());
    return h;
}

}  // namespace evo
