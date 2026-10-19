#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "core/SimulationConfig.hpp"
#include "organisms/Genome.hpp"
#include "organisms/Phenotype.hpp"

using namespace evo;
using Catch::Matchers::WithinAbs;

namespace {
std::array<bool, kNumTraits> noLocks() { return {}; }

bool inBounds(const Genome& g) {
    for (int i = 0; i < kNumTraits; ++i)
        if (!(g[i] >= geneSpec(i).min && g[i] <= geneSpec(i).max)) return false;
    for (std::size_t k = 0; k < g.brainSize(); ++k)
        if (std::fabs(g.brain()[k]) > kBrainWeightLimit) return false;
    return true;
}
}  // namespace

TEST_CASE("Gene table is consistent", "[genome]") {
    for (int i = 0; i < kNumTraits; ++i) {
        const auto& s = geneSpec(i);
        INFO(s.name);
        CHECK(s.min < s.max);
        CHECK(s.initMin >= s.min);
        CHECK(s.initMax <= s.max);
        CHECK(s.initMin <= s.initMax);
        CHECK(geneIndex(s.name) == i);
    }
    CHECK(geneIndex("nope") == -1);
}

TEST_CASE("Founder genomes respect their initial ranges", "[genome]") {
    Rng rng(1);
    const auto ranges = defaultInitRanges();
    for (int n = 0; n < 500; ++n) {
        Genome g = randomGenome(rng, ranges, 40);
        REQUIRE(g.genes.size() == kNumTraits + 40);
        for (int i = 0; i < kNumTraits; ++i) {
            CHECK(g[i] >= ranges[i].lo);
            CHECK(g[i] <= ranges[i].hi);
        }
        CHECK(inBounds(g));
    }
}

TEST_CASE("reflectIntoRange always lands inside the range", "[genome]") {
    Rng rng(2);
    for (int n = 0; n < 10000; ++n) {
        const double v = rng.normal(0.0, 50.0);
        const double r = reflectIntoRange(v, -1.0, 3.0);
        CHECK(r >= -1.0);
        CHECK(r <= 3.0);
    }
    CHECK(reflectIntoRange(2.0, 0.0, 3.0) == 2.0);
    CHECK_THAT(reflectIntoRange(3.5, 0.0, 3.0), WithinAbs(2.5, 1e-12));
    CHECK_THAT(reflectIntoRange(-0.5, 0.0, 3.0), WithinAbs(0.5, 1e-12));
    CHECK(reflectIntoRange(NAN, 0.0, 2.0) == 1.0);
}

TEST_CASE("Mutation: zero rate leaves genome unchanged", "[genome][mutation]") {
    Rng rng(3);
    MutationConfig mc;
    mc.mode = "fixed";
    mc.fixedRate = 0.0;
    Genome g = randomGenome(rng, defaultInitRanges(), 10);
    const Genome before = g;
    for (int i = 0; i < 100; ++i) CHECK(mutate(g, rng, mc, noLocks()) == 0);
    CHECK(g == before);
}

TEST_CASE("Mutation: probability, unbiasedness, spread and bounds", "[genome][mutation]") {
    Rng rng(4);
    MutationConfig mc;
    mc.mode = "fixed";
    mc.fixedRate = 0.1;
    mc.scale = 0.05;
    mc.largeProb = 0.0;
    const Genome base = randomGenome(rng, defaultInitRanges(), 0);
    const int trials = 4000;
    long changed = 0, total = 0;
    double sumDelta = 0, sumSq = 0;
    long nDelta = 0;
    for (int t = 0; t < trials; ++t) {
        Genome g = base;
        changed += mutate(g, rng, mc, noLocks());
        total += kNumTraits - 1;  // mutationRate gene is frozen in fixed mode
        REQUIRE(inBounds(g));
        // Speed sits mid-range, so reflection rarely triggers: use it for distribution checks.
        if (g[Speed] != base[Speed]) {
            const double d = (g[Speed] - base[Speed]) / (geneSpec(Speed).max - geneSpec(Speed).min);
            sumDelta += d;
            sumSq += d * d;
            ++nDelta;
        }
    }
    const double p = static_cast<double>(changed) / total;
    // Binomial: sd = sqrt(p(1-p)/n) ≈ 0.0011 → allow ~5 sd.
    CHECK_THAT(p, WithinAbs(0.1, 0.006));
    REQUIRE(nDelta > 200);
    const double mean = sumDelta / nDelta;
    const double sd = std::sqrt(sumSq / nDelta - mean * mean);
    CHECK_THAT(mean, WithinAbs(0.0, 0.01));  // not biased toward improvement or harm
    CHECK_THAT(sd, WithinAbs(0.05, 0.01));
}

TEST_CASE("Mutation: locked genes never change; evolvable rate uses the gene", "[genome][mutation]") {
    Rng rng(5);
    MutationConfig mc;  // evolvable
    std::array<bool, kNumTraits> locks{};
    locks[Vision] = true;
    locks[BodySize] = true;
    Genome g = randomGenome(rng, defaultInitRanges(), 0);
    g[MutationRate] = geneSpec(MutationRate).max;
    const double vision = g[Vision], size = g[BodySize];
    int changes = 0;
    for (int i = 0; i < 2000; ++i) changes += mutate(g, rng, mc, locks);
    CHECK(g[Vision] == vision);
    CHECK(g[BodySize] == size);
    CHECK(changes > 0);
    CHECK(inBounds(g));
}

TEST_CASE("Crossover takes each gene from one parent", "[genome]") {
    Rng rng(6);
    const Genome a = randomGenome(rng, defaultInitRanges(), 30);
    const Genome b = randomGenome(rng, defaultInitRanges(), 30);
    long fromA = 0, fromB = 0;
    for (int t = 0; t < 500; ++t) {
        const Genome c = crossover(a, b, rng);
        for (std::size_t k = 0; k < c.genes.size(); ++k) {
            REQUIRE((c.genes[k] == a.genes[k] || c.genes[k] == b.genes[k]));
            fromA += c.genes[k] == a.genes[k];
            fromB += c.genes[k] == b.genes[k];
        }
    }
    const double share = static_cast<double>(fromA) / (fromA + fromB);
    CHECK_THAT(share, WithinAbs(0.5, 0.02));
}

TEST_CASE("Genome distance is a normalised metric", "[genome]") {
    Rng rng(7);
    InitRanges full{};
    for (int i = 0; i < kNumTraits; ++i) full[i] = {geneSpec(i).min, geneSpec(i).max};
    Genome lo = randomGenome(rng, full, 0), hi = lo;
    for (int i = 0; i < kNumTraits; ++i) { lo[i] = geneSpec(i).min; hi[i] = geneSpec(i).max; }
    CHECK(genomeDistance(lo, lo) == 0.0);
    CHECK_THAT(genomeDistance(lo, hi), WithinAbs(1.0, 1e-12));
    for (int t = 0; t < 200; ++t) {
        const Genome a = randomGenome(rng, full, 0), b = randomGenome(rng, full, 0), c = randomGenome(rng, full, 0);
        CHECK(genomeDistance(a, b) == genomeDistance(b, a));
        CHECK(genomeDistance(a, c) <= genomeDistance(a, b) + genomeDistance(b, c) + 1e-12);
        CHECK(genomeDistance(a, b) <= 1.0);
    }
}

TEST_CASE("Phenotype trade-offs are monotone in the genes", "[phenotype]") {
    SimulationConfig cfg;
    Rng rng(8);
    Genome g = randomGenome(rng, defaultInitRanges(), 0);
    const Phenotype base = express(g, Kind::Prey, cfg);
    Genome bigger = g;
    bigger[BodySize] += 0.5;
    const Phenotype pb = express(bigger, Kind::Prey, cfg);
    CHECK(pb.maxEnergy > base.maxEnergy);
    CHECK(pb.basalCost > base.basalCost);
    CHECK(pb.moveCoeff > base.moveCoeff);
    CHECK(pb.biteRate > base.biteRate);
    Genome sharper = g;
    sharper[Vision] += 10;
    CHECK(express(sharper, Kind::Prey, cfg).basalCost > base.basalCost);  // vision is not free
    Genome efficient = g;
    efficient[Efficiency] += 0.2;
    const Phenotype pe = express(efficient, Kind::Prey, cfg);
    CHECK(pe.assimilation > base.assimilation);
    CHECK(pe.basalCost > base.basalCost);  // neither is a better gut
    CHECK(express(g, Kind::Predator, cfg).maxEnergy == base.maxEnergy * cfg.predators.storageMultiplier);
}
