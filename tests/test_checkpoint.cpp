#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "TestHelpers.hpp"
#include "serialization/Checkpoint.hpp"

using namespace evo;
using namespace evo::test;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("Checkpoint + resume is bit-identical to an uninterrupted run", "[checkpoint]") {
    for (const char* mode : {"asexual", "sexual"}) {
        INFO(mode);
        SimulationConfig c = smallConfig(31);
        c.reproduction.mode = mode;
        c.predators.enabled = true;
        c.predators.introduceAtTick = 150;
        c.predators.initialCount = 8;
        c.resources.distribution = "shifting";
        c.resources.shiftPeriod = 200;
        c.environment.events = {{250, "temperatureShift", 0.7}, {500, "addHazard", 0, {60, 60}, 20, 0.4}};
        Simulation full(c);
        full.run(700);

        Simulation first(c);
        first.run(333);
        EnvEvent e;  // interactive event before saving must survive the round trip
        e.type = "foodMultiplier";
        e.value = 0.8;
        first.scheduleEvent(e);
        Simulation fullWithEvent(first.config());
        fullWithEvent.run(700);

        const auto bytes = Checkpoint::save(first);
        auto resumed = Checkpoint::load(bytes);
        REQUIRE(resumed->tick() == 333);
        REQUIRE(resumed->stateHash() == first.stateHash());
        resumed->run(367);
        CHECK(resumed->stateHash() == fullWithEvent.stateHash());
        CHECK(resumed->stats().samples().size() == fullWithEvent.stats().samples().size());
        CHECK(resumed->stats().totalBirths() == fullWithEvent.stats().totalBirths());
        CHECK(resumed->lineage().size() == fullWithEvent.lineage().size());
        // Every recorded sample matches, except wall-clock timing.
        first.run(367);
        REQUIRE(resumed->stats().samples().size() == first.stats().samples().size());
        for (std::size_t k = 0; k < first.stats().samples().size(); ++k) {
            Json a = resumed->stats().samples()[k].toJson(), b = first.stats().samples()[k].toJson();
            a["stepMs"] = Json(0);
            b["stepMs"] = Json(0);
            REQUIRE(a.dump() == b.dump());
        }
        CHECK(resumed->ledger().toJson().dump() == first.ledger().toJson().dump());
    }
}

TEST_CASE("Corrupt or incompatible checkpoints are rejected clearly", "[checkpoint]") {
    Simulation s(smallConfig(32));
    s.run(50);
    auto bytes = Checkpoint::save(s);

    SECTION("bad magic") {
        bytes[0] = 'X';
        CHECK_THROWS_WITH(Checkpoint::load(bytes), ContainsSubstring("bad magic"));
    }
    SECTION("truncated") {
        bytes.resize(bytes.size() / 2);
        CHECK_THROWS_AS(Checkpoint::load(bytes), std::exception);
    }
    SECTION("trailing data") {
        bytes.push_back(0);
        CHECK_THROWS_WITH(Checkpoint::load(bytes), ContainsSubstring("trailing"));
    }
    SECTION("future format version") {
        bytes[8] = 99;
        CHECK_THROWS_WITH(Checkpoint::load(bytes), ContainsSubstring("not supported"));
    }
}
