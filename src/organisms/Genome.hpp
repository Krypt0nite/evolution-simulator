#pragma once
// Genotype: a flat vector of real-valued genes. The first kNumTraits entries
// are named traits with documented ranges; any remaining entries are the
// weights of the neural controller (present only when that controller is used).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Random.hpp"

namespace evo {

struct MutationConfig;

enum Gene : int {
    Speed = 0,       // max distance per tick
    Vision,          // perception radius
    Metabolism,      // metabolic rate: faster feeding, higher maintenance
    BodySize,        // mass: storage, bite size, defence; costs scale with it
    Efficiency,      // assimilation fraction of eaten biomass (costly gut)
    Fertility,       // per-tick probability of reproducing when eligible
    Maturity,        // age (ticks) at which reproduction becomes possible
    Lifespan,        // age at which senescent mortality sets in (costly to extend)
    RiskTolerance,   // 0 = flees/avoids readily, 1 = ignores danger
    Social,          // -1 avoids conspecifics, +1 seeks them
    MutationRate,    // per-gene mutation probability passed to offspring
    Investment,      // fraction of parent energy given to each offspring
    ThermalOptimum,  // preferred temperature; mismatch costs energy
    WForage,         // utility controller weights (heritable behaviour)
    WExplore,
    WRest,
    WFlee,
    WMate,
    kNumTraits
};

struct GeneSpec {
    const char* name;
    double min, max;          // valid range (hard constraint)
    double initMin, initMax;  // default founder range
    const char* description;
};

const GeneSpec& geneSpec(int gene);
int geneIndex(const std::string& name);  // -1 if unknown

inline constexpr double kBrainWeightLimit = 4.0;

struct Genome {
    std::vector<double> genes;  // size kNumTraits + brainSize

    double operator[](int g) const { return genes[static_cast<std::size_t>(g)]; }
    double& operator[](int g) { return genes[static_cast<std::size_t>(g)]; }
    std::size_t brainSize() const { return genes.size() - kNumTraits; }
    const double* brain() const { return genes.data() + kNumTraits; }
    bool operator==(const Genome&) const = default;
};

struct TraitRange {
    double lo, hi;
};
using InitRanges = std::array<TraitRange, kNumTraits>;

InitRanges defaultInitRanges();

Genome randomGenome(Rng& rng, const InitRanges& ranges, std::size_t brainSize);

// Mutates in place. `locked[g]` genes never change. If mode == fixed the
// mutation probability is cfg.fixedRate, otherwise the genome's own
// MutationRate gene. Returns the number of genes changed.
int mutate(Genome& g, Rng& rng, const MutationConfig& cfg, const std::array<bool, kNumTraits>& locked);

// Uniform crossover: each gene independently from either parent.
Genome crossover(const Genome& a, const Genome& b, Rng& rng);

// Euclidean distance over named traits, each normalised by its range, divided
// by sqrt(kNumTraits): 0 = identical, 1 = opposite corners of trait space.
double genomeDistance(const Genome& a, const Genome& b);

// Map a value back into [lo, hi] by reflection (avoids piling up at bounds).
double reflectIntoRange(double v, double lo, double hi);

}  // namespace evo
