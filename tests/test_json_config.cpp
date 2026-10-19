#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/Json.hpp"
#include "core/SimulationConfig.hpp"

using namespace evo;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("JSON parses and round-trips", "[json]") {
    const std::string text = R"({"a": 1.5, "b": [1, 2, {"c": "x\"y"}], "d": true, "e": null, // comment
        "f": -3e2})";
    Json j = Json::parse(text);
    CHECK(j.at("a").asNumber() == 1.5);
    CHECK(j.at("b").size() == 3);
    CHECK(j.at("b").asArray()[2].at("c").asString() == "x\"y");
    CHECK(j.at("d").asBool());
    CHECK(j.at("e").isNull());
    CHECK(j.at("f").asNumber() == -300.0);
    Json again = Json::parse(j.dump());
    CHECK(again.dump() == j.dump());
    CHECK(Json::parse(j.dump(2)).dump() == j.dump());
}

TEST_CASE("JSON numbers round-trip exactly", "[json]") {
    for (double d : {0.1, 1.0 / 3.0, 1e-300, 123456789.123456789, -2.5e17}) {
        CHECK(Json::parse(Json(d).dump()).asNumber() == d);
    }
}

TEST_CASE("JSON errors report position", "[json]") {
    CHECK_THROWS_WITH(Json::parse("{\n  \"a\": [1, 2,,]\n}"), ContainsSubstring("line 2"));
    CHECK_THROWS_AS(Json::parse("{\"a\": 1} trailing"), JsonError);
    CHECK_THROWS_AS(Json::parse("\"unterminated"), JsonError);
}

TEST_CASE("Json dotted paths", "[json]") {
    Json j = Json::object();
    j.setPath("a.b.c", Json(3));
    REQUIRE(j.findPath("a.b.c") != nullptr);
    CHECK(j.findPath("a.b.c")->asNumber() == 3);
    CHECK(j.findPath("a.x") == nullptr);
}

TEST_CASE("Default config is valid and round-trips", "[config]") {
    SimulationConfig c;
    CHECK(c.validate().empty());
    const Json j = c.toJson();
    const SimulationConfig back = SimulationConfig::fromJson(j);
    CHECK(back.toJson().dump() == j.dump());
}

TEST_CASE("Config rejects invalid input with explicit messages", "[config]") {
    Json j = SimulationConfig{}.toJson();

    SECTION("unknown keys") {
        j["resources"]["capacty"] = Json(3);
        CHECK_THROWS_WITH(SimulationConfig::fromJson(j), ContainsSubstring("resources.capacty: unknown key"));
    }
    SECTION("wrong type") {
        j["world"]["width"] = Json("wide");
        CHECK_THROWS_WITH(SimulationConfig::fromJson(j), ContainsSubstring("world.width: expected a number"));
    }
    SECTION("out of range") {
        j["mutation"]["fixedRate"] = Json(1.5);
        CHECK_THROWS_WITH(SimulationConfig::fromJson(j), ContainsSubstring("mutation.fixedRate"));
    }
    SECTION("incompatible version") {
        j["version"] = Json(99);
        CHECK_THROWS_WITH(SimulationConfig::fromJson(j), ContainsSubstring("not supported"));
    }
    SECTION("unknown gene") {
        j["population"]["lockedGenes"] = Json(Json::Array{Json("wings")});
        CHECK_THROWS_WITH(SimulationConfig::fromJson(j), ContainsSubstring("unknown gene"));
    }
    SECTION("trait range outside gene bounds") {
        j["population"]["initialTraits"]["speed"] = Json(Json::Array{Json(0.0), Json(9.0)});
        CHECK_THROWS_WITH(SimulationConfig::fromJson(j), ContainsSubstring("within gene range"));
    }
    SECTION("unknown event") {
        Json ev = Json::object();
        ev["tick"] = Json(10);
        ev["type"] = Json("meteor");
        j["environment"]["events"] = Json(Json::Array{ev});
        CHECK_THROWS_WITH(SimulationConfig::fromJson(j), ContainsSubstring("unknown type 'meteor'"));
    }
    SECTION("all problems reported at once") {
        j["world"]["width"] = Json(-1);
        j["resources"]["distribution"] = Json("spiral");
        try {
            SimulationConfig::fromJson(j);
            FAIL("expected ConfigError");
        } catch (const ConfigError& e) {
            const std::string m = e.what();
            CHECK_THAT(m, ContainsSubstring("world.width"));
            CHECK_THAT(m, ContainsSubstring("resources.distribution"));
        }
    }
}

TEST_CASE("Shipped config files are valid", "[config]") {
    for (const char* f : {"default", "predator_prey", "predator_prey_ecology", "sexual", "neural", "climate_shift",
                          "thermal_niches"}) {
        INFO(f);
        CHECK_NOTHROW(SimulationConfig::loadFile(std::string(EVOSIM_SOURCE_DIR) + "/config/" + f + ".json"));
    }
}
