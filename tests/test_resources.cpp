#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/Random.hpp"
#include "ecology/Environment.hpp"
#include "ecology/ResourceField.hpp"

using namespace evo;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

namespace {
SimulationConfig cfgFor(const std::string& dist) {
    SimulationConfig c;
    c.world.width = 120;
    c.world.height = 80;
    c.resources.distribution = dist;
    c.resources.patchCount = 3;
    return c;
}
}  // namespace

TEST_CASE("Regrowth is logistic, bounded by capacity, and fully accounted", "[resources]") {
    for (const char* dist : {"uniform", "clustered", "shifting"}) {
        INFO(dist);
        SimulationConfig c = cfgFor(dist);
        c.resources.initialFill = 0.1;
        Rng rng(1);
        ResourceField f;
        f.configure(c, rng);
        double total = f.total();
        for (int t = 0; t < 2000; ++t) {
            const auto flow = f.regrow(static_cast<std::uint64_t>(t));
            const double now = f.total();
            CHECK_THAT(now, WithinRel(total + flow.added - flow.decayed, 1e-9));
            total = now;
            for (std::size_t k = 0; k < f.cellCount(); ++k)
                REQUIRE(f.biomass(static_cast<int>(k)) <= f.capacityAt(static_cast<int>(k)) * (1 + 1e-12));
        }
        CHECK(f.total() > 0.9 * f.totalCapacity());  // approaches K
    }
}

TEST_CASE("No regrowth without growth or seeding", "[resources]") {
    SimulationConfig c = cfgFor("clustered");
    c.resources.regrowthRate = 0.0;
    c.resources.seedRate = 0.0;
    Rng rng(2);
    ResourceField f;
    f.configure(c, rng);
    const double before = f.total();
    for (int t = 0; t < 100; ++t) CHECK(f.regrow(static_cast<std::uint64_t>(t)).added == 0.0);
    CHECK(f.total() == before);
}

TEST_CASE("Depletion and recovery", "[resources]") {
    SimulationConfig c = cfgFor("uniform");
    Rng rng(3);
    ResourceField f;
    f.configure(c, rng);
    const int cell = f.cellAt({60, 40});
    const double taken = f.take(cell, 1e9);
    CHECK(taken > 0);
    CHECK(f.biomass(cell) == 0.0);
    CHECK(f.take(cell, 5.0) == 0.0);  // cannot go negative
    const double removed = f.depleteDisc({60, 40}, 20);
    CHECK(removed >= 0);
    for (int t = 0; t < 3000; ++t) f.regrow(static_cast<std::uint64_t>(t));
    CHECK(f.biomass(cell) > 0.5 * f.capacityAt(cell));  // seeding lets empty cells recover
}

TEST_CASE("Lowering capacity makes excess biomass decay", "[resources]") {
    SimulationConfig c = cfgFor("uniform");
    c.resources.initialFill = 1.0;
    Rng rng(4);
    ResourceField f;
    f.configure(c, rng);
    f.setMultiplier(0.25);
    double decayed = 0;
    for (int t = 0; t < 200; ++t) decayed += f.regrow(static_cast<std::uint64_t>(t)).decayed;
    CHECK(decayed > 0);
    CHECK(f.total() < 0.4 * c.resources.capacity * f.cellCount());
}

TEST_CASE("Temperature field and hazards", "[environment]") {
    SimulationConfig c;
    c.environment.temperatureGradient = 1.0;
    c.environment.hazards.push_back({{100, 100}, 10, 0.5});
    Environment env;
    env.configure(c);
    CHECK_THAT(env.temperature({0, 0}), WithinAbs(-1.0, 1e-12));
    CHECK_THAT(env.temperature({0, c.world.height}), WithinAbs(1.0, 1e-12));
    env.setOffset(0.5);
    CHECK_THAT(env.temperature({0, c.world.height / 2}), WithinAbs(0.5, 1e-12));
    CHECK(env.hazardDrain({100, 105}) == 0.5);
    CHECK(env.hazardDrain({100, 111}) == 0.0);
}
