#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestHelpers.hpp"

using namespace evo;
using namespace evo::test;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

TEST_CASE("Same seed and config give identical trajectories", "[simulation][determinism]") {
    for (const char* mode : {"asexual", "sexual"}) {
        for (const char* ctrl : {"utility", "neural"}) {
            INFO(mode << " / " << ctrl);
            SimulationConfig c = smallConfig(99);
            c.reproduction.mode = mode;
            c.controller.type = ctrl;
            c.predators.enabled = true;
            c.predators.introduceAtTick = 100;
            c.predators.initialCount = 8;
            Simulation a(c), b(c);
            for (int t = 0; t < 400; ++t) {
                a.step();
                b.step();
                if (t % 50 == 0) REQUIRE(a.stateHash() == b.stateHash());
            }
            CHECK(a.stateHash() == b.stateHash());
            CHECK(a.stats().totalBirths() == b.stats().totalBirths());
        }
    }
}

TEST_CASE("Different seeds give different trajectories", "[simulation][determinism]") {
    Simulation a(smallConfig(1)), b(smallConfig(2));
    a.run(50);
    b.run(50);
    CHECK(a.stateHash() != b.stateHash());
}

TEST_CASE("Analytics settings do not perturb the trajectory", "[simulation][determinism]") {
    SimulationConfig c1 = smallConfig(5), c2 = smallConfig(5);
    c2.analytics.sampleInterval = 7;
    c2.analytics.clusterInterval = 13;
    Simulation a(c1), b(c2);
    a.run(300);
    b.run(300);
    CHECK(a.stateHash() == b.stateHash());
}

TEST_CASE("Complete ticks produce births and deaths", "[simulation][integration]") {
    Simulation s(smallConfig());
    s.run(1500);
    CHECK(s.stats().totalBirths() > 50);
    CHECK(s.stats().totalDeaths() > 50);
    CHECK(s.tick() == 1500);
    CHECK(!s.organisms().empty());
    CHECK(s.makeSample().maxGeneration >= 2);
}

TEST_CASE("Energy ledger balances in every scenario", "[simulation][energy]") {
    struct Variant { const char* name; void (*apply)(SimulationConfig&); };
    const Variant variants[] = {
        {"default", [](SimulationConfig&) {}},
        {"sexual", [](SimulationConfig& c) { c.reproduction.mode = "sexual"; }},
        {"neural", [](SimulationConfig& c) { c.controller.type = "neural"; }},
        {"predators", [](SimulationConfig& c) { c.predators.enabled = true; c.predators.introduceAtTick = 50; c.predators.initialCount = 10; }},
        {"events+hazards", [](SimulationConfig& c) {
             c.environment.hazards.push_back({{100, 75}, 25, 0.8});
             c.environment.events = {{200, "foodMultiplier", 0.3}, {300, "temperatureShift", 1.2},
                                     {350, "removeRandom", 0, {}, 0, 0, 20}, {400, "introducePredators", 0, {}, 0, 0, 5},
                                     {450, "depleteRegion", 0, {100, 75}, 50}, {500, "shiftResources"},
                                     {600, "foodMultiplier", 1.0}};
             c.predators.enabled = true;
             c.predators.introduceAtTick = 100000;
         }},
        {"seasons", [](SimulationConfig& c) { c.resources.seasonAmplitude = 0.6; c.resources.seasonPeriod = 300; c.environment.cycleAmplitude = 0.5; }},
    };
    for (const auto& v : variants) {
        INFO(v.name);
        SimulationConfig c = smallConfig(3);
        v.apply(c);
        Simulation s(c);
        CHECK_THAT(totalEnergy(s), WithinRel(ledgerExpected(s), 1e-9));
        for (int t = 0; t < 800; ++t) {
            s.step();
            if (t % 100 == 0) REQUIRE_THAT(totalEnergy(s), WithinRel(ledgerExpected(s), 1e-9));
        }
        CHECK_THAT(totalEnergy(s), WithinRel(ledgerExpected(s), 1e-9));
        // Flows have the right sign.
        const auto& L = s.ledger();
        for (double f : {L.inflow, L.introduced, L.decay, L.digestion, L.basal, L.movement, L.thermal, L.hazard,
                         L.hunting, L.reproduction, L.overflow, L.carcass})
            CHECK(f >= 0.0);
    }
}

TEST_CASE("Lineage bookkeeping agrees with the living population", "[simulation][lineage]") {
    SimulationConfig c = smallConfig(4);
    c.reproduction.mode = "sexual";
    Simulation s(c);
    s.run(1200);
    const auto& recs = s.lineage().records();
    REQUIRE(recs.size() == s.nextId() - 1);
    std::size_t alive = 0;
    for (const auto& r : recs) alive += r.deathTick == UINT32_MAX;
    CHECK(alive == s.organisms().size());
    CHECK(s.stats().totalDeaths() + s.organisms().size() == recs.size());
    // Offspring counts recorded on parents match children pointing at them.
    std::vector<std::uint32_t> counted(recs.size() + 1, 0);
    for (const auto& r : recs) {
        if (r.parentA) ++counted[r.parentA];
        if (r.parentB && r.parentB != r.parentA) ++counted[r.parentB];
    }
    for (std::size_t id = 1; id <= recs.size(); ++id) REQUIRE(counted[id] == recs[id - 1].offspring);
    // Descendant count is consistent with direct children.
    std::uint64_t someParent = 0;
    for (std::size_t id = 1; id <= recs.size(); ++id) if (recs[id - 1].offspring > 0) { someParent = id; break; }
    REQUIRE(someParent);
    CHECK(s.lineage().descendantCount(someParent) >= s.lineage().children(someParent, 1000).size());
}

TEST_CASE("Predators hunt, gain energy and can starve", "[simulation][predators]") {
    SimulationConfig c = smallConfig(8);
    c.predators.enabled = true;
    c.predators.introduceAtTick = 0;
    c.predators.initialCount = 15;
    Simulation s(c);
    REQUIRE(s.predatorCount() == 15);
    int kills = 0;
    for (int t = 0; t < 1500; ++t) {
        s.step();
        if (s.stats().latest() && s.tick() % c.analytics.sampleInterval == 0) kills += s.stats().latest()->deathsPredation;
    }
    CHECK(kills > 0);
    CHECK(s.ledger().assimilatedPrey > 0);
    CHECK(s.ledger().hunting > 0);
}

TEST_CASE("Capture probability parameter controls predation", "[simulation][predators]") {
    SimulationConfig c = smallConfig(8);
    c.predators.enabled = true;
    c.predators.introduceAtTick = 0;
    c.predators.initialCount = 15;
    c.predators.captureBase = 0.0;
    Simulation s(c);
    s.run(800);
    CHECK(s.ledger().assimilatedPrey == 0.0);
    for (const auto& smp : s.stats().samples()) CHECK(smp.deathsPredation == 0);
}

TEST_CASE("Interactive events are recorded so config + seed replays them", "[simulation][events]") {
    SimulationConfig c = smallConfig(12);
    Simulation live(c);
    live.run(100);
    EnvEvent e;
    e.type = "foodMultiplier";
    e.value = 0.2;
    live.scheduleEvent(e);
    live.run(100);
    EnvEvent h;
    h.type = "addHazard";
    h.pos = {50, 50};
    h.radius = 30;
    h.damage = 1.0;
    live.scheduleEvent(h);
    live.run(100);

    Simulation replay(live.config());
    replay.run(300);
    CHECK(replay.stateHash() == live.stateHash());
    CHECK(replay.config().environment.events.size() == 2);
}

TEST_CASE("Perception only reports things inside the vision radius", "[simulation][perception]") {
    SimulationConfig c = smallConfig(13);
    c.predators.enabled = true;
    c.predators.introduceAtTick = 0;
    c.predators.initialCount = 20;
    c.environment.hazards.push_back({{100, 75}, 15, 0.1});
    Simulation s(c);
    s.run(30);
    for (std::size_t i = 0; i < s.organisms().size(); ++i) {
        const auto& o = s.organisms()[i];
        const Perception p = s.perceive(i);
        const double v = o.pheno.vision + 1e-9;
        if (p.foodSeen) CHECK(p.foodDist <= v);
        if (p.threatSeen) CHECK(p.threatDist <= v);
        if (p.preySeen) CHECK(p.preyDist <= v);
        if (p.mateSeen) CHECK(p.mateDist <= v);
        if (p.hazardSeen) CHECK(distance(o.pos, {100, 75}) - 15 <= v);
        CHECK(p.neighbours <= c.organisms.attentionLimit);
    }
}

TEST_CASE("Population cap suppresses births", "[simulation]") {
    SimulationConfig c = smallConfig(14);
    c.population.maxPopulation = 130;
    Simulation s(c);
    for (int t = 0; t < 600; ++t) {
        s.step();
        REQUIRE(s.organisms().size() <= 130);
    }
}

TEST_CASE("Shifting resources relocate on schedule; clustered ones stay put", "[simulation][resources]") {
    SimulationConfig shifting = smallConfig(41), clustered = smallConfig(41);
    shifting.resources.distribution = "shifting";
    shifting.resources.shiftPeriod = 100;
    clustered.resources.distribution = "clustered";
    Simulation a(shifting), b(clustered);
    const auto patchesA0 = a.food().patches(), patchesB0 = b.food().patches();
    REQUIRE(patchesA0 == patchesB0);  // same seed, same initial layout
    a.run(99);
    b.run(99);
    CHECK(a.food().patches() == patchesA0);  // not yet
    a.run(2);
    b.run(2);
    CHECK(a.food().patches() != patchesA0);  // relocated at tick 100
    CHECK(b.food().patches() == patchesB0);  // static patches never move
    a.run(100);
    const auto afterSecond = a.food().patches();
    CHECK(afterSecond != patchesA0);
}
