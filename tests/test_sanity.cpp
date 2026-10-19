// Scientific sanity checks: simple scenarios with an expected *qualitative*
// outcome. None of them asserts which trait "should" evolve.
#include <catch2/catch_test_macros.hpp>

#include <map>

#include "TestHelpers.hpp"

using namespace evo;
using namespace evo::test;

TEST_CASE("Without replenishing resources the population runs out of food", "[sanity]") {
    SimulationConfig c = smallConfig(21);
    c.resources.regrowthRate = 0.0;
    c.resources.seedRate = 0.0;
    Simulation s(c);
    double prevFood = s.food().total();
    std::size_t peak = 0;
    for (int t = 0; t < 20000 && !s.organisms().empty(); ++t) {
        s.step();
        REQUIRE(s.food().total() <= prevFood + 1e-9);  // food never increases
        prevFood = s.food().total();
        peak = std::max(peak, s.organisms().size());
    }
    CHECK(s.organisms().empty());   // finite energy → eventual extinction
    CHECK(s.ledger().inflow == 0.0);
    CHECK(peak > 0);
}

TEST_CASE("Without energy acquisition organisms cannot survive indefinitely", "[sanity]") {
    SimulationConfig c = smallConfig(22);
    c.resources.initialFill = 0.0;
    c.resources.regrowthRate = 0.0;
    c.resources.seedRate = 0.0;
    Simulation s(c);
    // Upper bound on survival: max storage / min possible maintenance.
    s.run(5000);
    CHECK(s.organisms().empty());
    CHECK(s.ledger().assimilatedPlants == 0.0);
}

TEST_CASE("If reproduction is impossible there are no births", "[sanity]") {
    SimulationConfig c = smallConfig(23);
    c.reproduction.enabled = false;
    Simulation s(c);
    std::size_t prev = s.organisms().size();
    for (int t = 0; t < 1500; ++t) {
        s.step();
        REQUIRE(s.organisms().size() <= prev);
        prev = s.organisms().size();
    }
    CHECK(s.stats().totalBirths() == 0);
    CHECK(s.nextId() == static_cast<std::uint64_t>(c.population.initialPrey) + 1);
}

TEST_CASE("Sexual reproduction without compatible mates produces no births", "[sanity]") {
    SimulationConfig c = smallConfig(24);
    c.reproduction.mode = "sexual";
    c.reproduction.compatibilityThreshold = 1e-9;  // nobody is compatible
    Simulation s(c);
    s.run(1000);
    CHECK(s.stats().totalBirths() == 0);
}

TEST_CASE("With no mutation, asexual offspring inherit traits unchanged", "[sanity]") {
    SimulationConfig c = smallConfig(25);
    c.mutation.mode = "fixed";
    c.mutation.fixedRate = 0.0;
    Simulation s(c);
    std::map<std::uint64_t, Genome> founders;
    for (const auto& o : s.organisms()) founders[o.id] = o.genome;
    s.run(1500);
    REQUIRE(s.stats().totalBirths() > 0);
    for (const auto& o : s.organisms()) {
        REQUIRE(founders.count(o.founder));
        REQUIRE(o.genome == founders[o.founder]);
    }
}

TEST_CASE("Mutation introduces new genotypes; selection can then change trait means", "[sanity]") {
    SimulationConfig c = smallConfig(26);
    Simulation s(c);
    const Sample start = s.makeSample();
    s.run(4000);
    const Sample end = s.makeSample();
    REQUIRE(end.population > 0);
    // Something moved – which trait and in which direction is NOT asserted.
    double moved = 0;
    for (int g = 0; g < kNumTraits; ++g) {
        const auto& spec = geneSpec(g);
        moved = std::max(moved, std::fabs(end.traitMean[g] - start.traitMean[g]) / (spec.max - spec.min));
    }
    CHECK(moved > 0.01);
}

TEST_CASE("Temperature shift raises thermal costs for an adapted population", "[sanity][environment]") {
    SimulationConfig base = smallConfig(27);
    base.environment.temperatureGradient = 0.0;
    base.population.initialTraits["thermalOptimum"] = {0.0, 0.0};
    SimulationConfig shifted = base;
    shifted.environment.events.push_back({200, "temperatureShift", 1.2});
    Simulation a(base), b(shifted);
    a.run(200);
    b.run(200);
    const double ta0 = a.ledger().thermal, tb0 = b.ledger().thermal;
    a.run(50);
    b.run(50);
    // Mechanism only: the shift makes maintaining the same organisms costlier.
    CHECK((b.ledger().thermal - tb0) > 5.0 * (a.ledger().thermal - ta0) + 1e-9);
}

TEST_CASE("Predation outcomes depend on interaction parameters, not a script", "[sanity][predators]") {
    auto kills = [](double capture, std::uint64_t seed) {
        SimulationConfig c = smallConfig(seed);
        c.predators.enabled = true;
        c.predators.introduceAtTick = 0;
        c.predators.initialCount = 12;
        c.predators.captureBase = capture;
        Simulation s(c);
        s.run(600);
        return s.ledger().assimilatedPrey;
    };
    double lo = 0, hi = 0;
    for (std::uint64_t seed = 1; seed <= 3; ++seed) { lo += kills(0.05, seed); hi += kills(0.9, seed); }
    CHECK(hi > lo);
}
