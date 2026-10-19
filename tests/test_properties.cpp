// Property-style tests: run many short randomised simulations and check that
// structural invariants hold after every single tick.
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "TestHelpers.hpp"

using namespace evo;
using namespace evo::test;

namespace {
SimulationConfig randomConfig(Rng& r, std::uint64_t seed) {
    SimulationConfig c = smallConfig(seed);
    c.reproduction.mode = r.chance(0.5) ? "sexual" : "asexual";
    c.controller.type = r.chance(0.3) ? "neural" : "utility";
    c.resources.distribution = r.chance(0.33) ? "uniform" : (r.chance(0.5) ? "clustered" : "shifting");
    c.resources.shiftPeriod = 150;
    c.mutation.mode = r.chance(0.5) ? "fixed" : "evolvable";
    c.mutation.fixedRate = r.uniform(0.0, 0.5);
    c.mutation.largeProb = r.uniform(0.0, 0.5);
    c.predators.enabled = r.chance(0.5);
    c.predators.introduceAtTick = static_cast<int>(r.uniform(0, 100));
    c.predators.initialCount = 10;
    c.population.maxPopulation = 400;
    c.environment.temperatureGradient = r.uniform(0, 1.5);
    c.environment.cycleAmplitude = r.uniform(0, 1);
    c.environment.cyclePeriod = 200;
    c.resources.seasonAmplitude = r.uniform(0, 0.8);
    c.resources.seasonPeriod = 250;
    if (r.chance(0.5)) c.environment.hazards.push_back({{r.uniform(0, 200), r.uniform(0, 150)}, 20, r.uniform(0, 1)});
    return c;
}
}  // namespace

TEST_CASE("Invariants hold after every tick across random configurations", "[property]") {
    Rng meta(2024);
    for (int trial = 0; trial < 12; ++trial) {
        const SimulationConfig c = randomConfig(meta, 500 + static_cast<std::uint64_t>(trial));
        INFO("trial " << trial << " mode=" << c.reproduction.mode << " ctrl=" << c.controller.type
                      << " predators=" << c.predators.enabled);
        Simulation s(c);
        std::uint64_t lastNextId = s.nextId();
        for (int t = 0; t < 300; ++t) {
            s.step();
            const auto& orgs = s.organisms();
            // Population counts agree with the actual living population.
            const Sample smp = s.makeSample();
            REQUIRE(smp.population == static_cast<int>(orgs.size()));
            REQUIRE(smp.prey + smp.predators == smp.population);
            REQUIRE(s.nextId() >= lastNextId);
            lastNextId = s.nextId();
            std::uint64_t prevId = 0;
            for (const auto& o : orgs) {
                // Only living organisms remain after commit; ids strictly ascending.
                REQUIRE(o.alive);
                REQUIRE(o.id > prevId);
                prevId = o.id;
                // No invalid trait values.
                for (int g = 0; g < kNumTraits; ++g) {
                    REQUIRE(std::isfinite(o.genome[g]));
                    REQUIRE(o.genome[g] >= geneSpec(g).min);
                    REQUIRE(o.genome[g] <= geneSpec(g).max);
                }
                for (std::size_t k = 0; k < o.genome.brainSize(); ++k)
                    REQUIRE(std::fabs(o.genome.brain()[k]) <= kBrainWeightLimit);
                // Energy within documented bounds; position inside the world.
                REQUIRE(o.energy > 0.0);
                REQUIRE(o.energy <= o.pheno.maxEnergy * (1 + 1e-12));
                REQUIRE(o.pos.x >= 0.0);
                REQUIRE(o.pos.x < c.world.width);
                REQUIRE(o.pos.y >= 0.0);
                REQUIRE(o.pos.y < c.world.height);
                // Every child references valid parent history.
                const LineageRecord* rec = s.lineage().get(o.id);
                REQUIRE(rec != nullptr);
                REQUIRE(rec->deathTick == UINT32_MAX);
                if (o.parentA) {
                    REQUIRE(o.parentA < o.id);
                    const LineageRecord* pa = s.lineage().get(o.parentA);
                    REQUIRE(pa != nullptr);
                    REQUIRE(pa->kind == static_cast<std::uint8_t>(o.kind));
                    std::uint32_t parentGen = pa->generation;
                    if (o.parentB) {
                        REQUIRE(o.parentB < o.id);
                        REQUIRE(s.lineage().get(o.parentB) != nullptr);
                        parentGen = std::max(parentGen, s.lineage().get(o.parentB)->generation);
                    }
                    REQUIRE(o.generation == parentGen + 1);
                    REQUIRE(o.founder == pa->founder);
                } else {
                    REQUIRE(o.founder == o.id);
                    REQUIRE(o.generation == 0);
                }
            }
        }
        // Dead organisms cannot act: no child was ever born at or after its
        // parent's death tick.
        const auto& recs = s.lineage().records();
        for (const auto& r : recs) {
            if (r.parentA) {
                const auto& pa = recs[r.parentA - 1];
                REQUIRE(r.birthTick < pa.deathTick);
            }
            if (r.parentB) {
                const auto& pb = recs[r.parentB - 1];
                REQUIRE(r.birthTick < pb.deathTick);
            }
        }
    }
}
