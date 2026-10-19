#include "organisms/Genome.hpp"

#include <cmath>
#include <stdexcept>

#include "core/SimulationConfig.hpp"

namespace evo {

namespace {
// name, min, max, initMin, initMax, description
const std::array<GeneSpec, kNumTraits> kSpecs = {{
    {"speed", 0.2, 4.0, 0.8, 1.6, "max movement per tick (world units); move cost ∝ size·v²"},
    {"vision", 2.0, 60.0, 10.0, 25.0, "perception radius (world units); maintenance ∝ radius"},
    {"metabolism", 0.3, 3.0, 0.8, 1.2, "metabolic rate: bite rate ∝ √m, maintenance ∝ m"},
    {"size", 0.5, 3.0, 0.9, 1.3, "body size: storage, bite, defence; costs ∝ size^0.75 / size"},
    {"efficiency", 0.2, 0.95, 0.4, 0.6, "fraction of eaten biomass assimilated; gut maintenance ∝ 0.5 + e"},
    {"fertility", 0.02, 1.0, 0.1, 0.3, "per-tick probability of reproducing when eligible"},
    {"maturity", 10.0, 800.0, 80.0, 160.0, "age (ticks) at which reproduction becomes possible"},
    {"lifespan", 100.0, 6000.0, 800.0, 1500.0, "age (ticks) where senescent mortality starts; maintenance ∝ lifespan"},
    {"riskTolerance", 0.0, 1.0, 0.3, 0.7, "0 = flee/avoid danger readily, 1 = ignore danger"},
    {"social", -1.0, 1.0, -0.2, 0.2, "attraction (+) or aversion (-) to conspecifics"},
    {"mutationRate", 0.001, 0.3, 0.03, 0.08, "per-gene mutation probability in offspring"},
    {"investment", 0.1, 0.7, 0.3, 0.45, "fraction of parent energy given to each offspring"},
    {"thermalOptimum", -1.5, 1.5, -0.2, 0.2, "preferred temperature; cost ∝ (T - optimum)²"},
    {"wForage", 0.0, 2.0, 0.8, 1.2, "utility weight: approach visible food"},
    {"wExplore", 0.0, 2.0, 0.4, 0.8, "utility weight: wander/search"},
    {"wRest", 0.0, 2.0, 0.2, 0.6, "utility weight: stay still to save energy"},
    {"wFlee", 0.0, 2.0, 0.8, 1.2, "utility weight: flee predators / avoid hazards"},
    {"wMate", 0.0, 2.0, 0.6, 1.0, "utility weight: approach a ready mate (sexual mode)"},
}};
}  // namespace

const GeneSpec& geneSpec(int gene) {
    if (gene < 0 || gene >= kNumTraits) throw std::out_of_range("gene index");
    return kSpecs[static_cast<std::size_t>(gene)];
}

int geneIndex(const std::string& name) {
    for (int i = 0; i < kNumTraits; ++i)
        if (name == kSpecs[static_cast<std::size_t>(i)].name) return i;
    return -1;
}

InitRanges defaultInitRanges() {
    InitRanges r{};
    for (int i = 0; i < kNumTraits; ++i) r[static_cast<std::size_t>(i)] = {kSpecs[i].initMin, kSpecs[i].initMax};
    return r;
}

double reflectIntoRange(double v, double lo, double hi) {
    if (!(hi > lo)) return lo;
    if (!std::isfinite(v)) return 0.5 * (lo + hi);
    const double w = hi - lo;
    double t = std::fmod(v - lo, 2.0 * w);
    if (t < 0) t += 2.0 * w;
    double r = t <= w ? lo + t : hi - (t - w);
    // Guard against rounding just outside the bounds.
    return r < lo ? lo : (r > hi ? hi : r);
}

Genome randomGenome(Rng& rng, const InitRanges& ranges, std::size_t brainSize) {
    Genome g;
    g.genes.resize(kNumTraits + brainSize);
    for (int i = 0; i < kNumTraits; ++i) {
        const auto& r = ranges[static_cast<std::size_t>(i)];
        g[i] = r.lo == r.hi ? r.lo : rng.uniform(r.lo, r.hi);
    }
    // Neural weights: small Gaussian, Xavier-like scale, clamped.
    for (std::size_t k = 0; k < brainSize; ++k) {
        double w = rng.normal(0.0, 0.7);
        g.genes[kNumTraits + k] = w < -kBrainWeightLimit ? -kBrainWeightLimit : (w > kBrainWeightLimit ? kBrainWeightLimit : w);
    }
    return g;
}

int mutate(Genome& g, Rng& rng, const MutationConfig& cfg, const std::array<bool, kNumTraits>& locked) {
    const double p = cfg.mode == "fixed" ? cfg.fixedRate : g[MutationRate];
    int changed = 0;
    for (std::size_t k = 0; k < g.genes.size(); ++k) {
        if (!rng.chance(p)) continue;
        double lo, hi;
        if (k < kNumTraits) {
            if (locked[k]) continue;
            if (cfg.mode == "fixed" && static_cast<int>(k) == MutationRate) continue;
            lo = kSpecs[k].min;
            hi = kSpecs[k].max;
        } else {
            lo = -kBrainWeightLimit;
            hi = kBrainWeightLimit;
        }
        const double range = hi - lo;
        const double sd = (rng.chance(cfg.largeProb) ? cfg.largeScale : cfg.scale) * range;
        g.genes[k] = reflectIntoRange(g.genes[k] + rng.normal(0.0, sd), lo, hi);
        ++changed;
    }
    return changed;
}

Genome crossover(const Genome& a, const Genome& b, Rng& rng) {
    Genome c = a;
    const std::size_t n = std::min(a.genes.size(), b.genes.size());
    for (std::size_t k = 0; k < n; ++k)
        if (rng.uniform() < 0.5) c.genes[k] = b.genes[k];
    return c;
}

double genomeDistance(const Genome& a, const Genome& b) {
    double s = 0.0;
    for (int i = 0; i < kNumTraits; ++i) {
        const double d = (a[i] - b[i]) / (kSpecs[i].max - kSpecs[i].min);
        s += d * d;
    }
    return std::sqrt(s / static_cast<double>(kNumTraits));
}

}  // namespace evo
