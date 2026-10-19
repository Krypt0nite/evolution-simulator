#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>

#include "analytics/ExperimentRunner.hpp"
#include "analytics/StatsMath.hpp"
#include "TestHelpers.hpp"

using namespace evo;
using Catch::Matchers::WithinAbs;

TEST_CASE("Statistics helpers match known values", "[stats]") {
    const std::vector<double> v = {2, 4, 4, 4, 5, 5, 7, 9};
    CHECK_THAT(stats::mean(v), WithinAbs(5.0, 1e-12));
    CHECK_THAT(stats::sd(v), WithinAbs(2.138089935, 1e-8));
    CHECK_THAT(stats::median(v), WithinAbs(4.5, 1e-12));
    const auto ci = stats::meanCI95(v);  // t(7) = 2.365
    CHECK_THAT(ci.lo, WithinAbs(5.0 - 2.365 * 2.138089935 / std::sqrt(8.0), 1e-6));
    CHECK_THAT(stats::tCritical95(10), WithinAbs(2.228, 1e-9));
    CHECK_THAT(stats::tCritical95(1000), WithinAbs(1.96, 1e-9));
    // Cohen's d of two groups 1 SD apart.
    CHECK_THAT(stats::cohensD({1, 2, 3}, {2, 3, 4}), WithinAbs(1.0, 1e-12));
    // Wilson interval for 0/10 includes 0 and is not degenerate.
    const auto w = stats::wilson95(0, 10);
    CHECK(w.lo == 0.0);
    CHECK_THAT(w.hi, WithinAbs(0.2775, 1e-3));
    // Welch p for identical samples is 1, for clearly separated samples is tiny.
    CHECK_THAT(stats::welchP({1, 2, 3, 4}, {1, 2, 3, 4}), WithinAbs(1.0, 1e-9));
    CHECK(stats::welchP({1, 2, 3, 4, 5}, {11, 12, 13, 14, 15}) < 1e-5);
    // Reference: t = 2.5, df = 8 → two-sided p = 0.036942 (numerical integration of the t pdf).
    CHECK_THAT(stats::welchP({1, 2, 3, 4, 5}, {3.5, 4.5, 5.5, 6.5, 7.5}), WithinAbs(0.036942, 1e-5));
}

namespace {
ExperimentSpec tinySpec() {
    Json j = Json::object();
    j["name"] = Json("tiny");
    j["base"] = evo::test::smallConfig().toJson();
    j["ticks"] = Json(300);
    j["seeds"] = Json(3);
    j["sampleInterval"] = Json(50);
    Json conds = Json::array();
    Json a = Json::object();
    a["name"] = Json("low");
    a["overrides"]["mutation.mode"] = Json("fixed");
    a["overrides"]["mutation.fixedRate"] = Json(0.0);
    Json b = Json::object();
    b["name"] = Json("high");
    b["overrides"]["mutation.mode"] = Json("fixed");
    b["overrides"]["mutation.fixedRate"] = Json(0.3);
    conds.push(a);
    conds.push(b);
    j["conditions"] = conds;
    return ExperimentSpec::fromJson(j, "");
}
}  // namespace

TEST_CASE("Experiment runner executes independent seeds deterministically", "[experiment]") {
    ExperimentRunner runner(tinySpec());
    REQUIRE(runner.runCount() == 6);
    const auto serial = runner.runAll(1);
    const auto parallel = runner.runAll(3);
    REQUIRE(serial.size() == 6);
    for (std::size_t i = 0; i < serial.size(); ++i) {
        CHECK(serial[i].condition == parallel[i].condition);
        CHECK(serial[i].seed == parallel[i].seed);
        CHECK(serial[i].totalBirths == parallel[i].totalBirths);
        CHECK(serial[i].series == parallel[i].series);
    }
    // Conditions share seeds; different seeds differ.
    CHECK(serial[0].seed == serial[3].seed);
    CHECK(serial[0].series != serial[1].series);
    // Every run has the same number of time-series rows (padding for extinct runs).
    for (const auto& r : serial) CHECK(r.series.size() == serial[0].series.size());
}

TEST_CASE("Experiment summaries and CSV outputs", "[experiment]") {
    ExperimentRunner runner(tinySpec());
    const auto results = runner.runAll(2);
    const Json summary = runner.summarize(results);
    REQUIRE(summary.at("conditions").size() == 2);
    const auto& high = summary.at("conditions").asArray()[1];
    CHECK(high.at("metrics").at("mean_population").at("n").asNumber() == 3);
    CHECK(high.at("metrics").at("mean_population").contains("dVsBaseline"));
    CHECK(high.at("timeseries").at("tick").size() == results[0].series.size());

    const auto dir = std::filesystem::temp_directory_path() / "evosim_test_experiment";
    std::filesystem::remove_all(dir);
    runner.writeOutputs(dir.string(), results, 1.0);
    for (const char* f : {"runs.csv", "timeseries.csv", "timeseries_summary.csv", "summary.csv", "summary.json",
                          "summary.md", "meta.json", "spec.json"})
        CHECK(std::filesystem::exists(dir / f));
    std::ifstream runs(dir / "runs.csv");
    std::string header;
    std::getline(runs, header);
    CHECK(header.rfind("condition,seed,ticks_run,extinction_tick,extinct,", 0) == 0);
    int lines = 0;
    for (std::string l; std::getline(runs, l);) ++lines;
    CHECK(lines == 6);
}

TEST_CASE("Experiment specs are validated before running", "[experiment]") {
    Json j = Json::object();
    j["base"] = evo::test::smallConfig().toJson();
    Json c = Json::object();
    c["name"] = Json("typo");
    c["overrides"]["mutation.fixdRate"] = Json(0.1);
    j["conditions"] = Json(Json::Array{c});
    CHECK_THROWS_AS(ExperimentSpec::fromJson(j, ""), ConfigError);
}

TEST_CASE("Shipped experiment specs parse", "[experiment]") {
    for (const auto& entry : std::filesystem::directory_iterator(std::string(EVOSIM_SOURCE_DIR) + "/config/experiments")) {
        INFO(entry.path().string());
        CHECK_NOTHROW(ExperimentSpec::loadFile(entry.path().string()));
    }
}
