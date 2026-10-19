#pragma once
// Headless batch experiments: every condition × seed is an independent run.
// Conditions share seeds (common random numbers), so differences between
// conditions are not confounded by different random initial placements.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/Json.hpp"
#include "core/SimulationConfig.hpp"

namespace evo {

struct ExperimentCondition {
    std::string name;
    Json overrides = Json::object();  // dotted path -> value
};

struct ExperimentSpec {
    std::string name;
    std::string description;
    std::string hypothesis;
    Json base = Json::object();
    std::uint64_t ticks = 5000;
    std::vector<std::uint64_t> seeds;
    int sampleInterval = 100;
    int recoveryAfterTick = -1;   // tick of a perturbation; measures recovery time
    std::vector<ExperimentCondition> conditions;

    static ExperimentSpec fromJson(const Json& j, const std::string& baseDir);
    static ExperimentSpec loadFile(const std::string& path);
    Json toJson() const;
    SimulationConfig configFor(std::size_t condition, std::uint64_t seed) const;
};

struct RunResult {
    std::string condition;
    std::size_t conditionIndex = 0;
    std::uint64_t seed = 0;
    std::uint64_t ticksRun = 0;
    bool extinct = false;
    bool predatorsExtinct = false;
    std::uint64_t extinctionTick = 0;
    double finalPopulation = 0, finalPrey = 0, finalPredators = 0;
    double meanPopulation = 0;      // over the second half of the run
    double minPopulation = 0;
    double recoveryTicks = -1;      // -1 = no perturbation or never recovered
    double meanLifespan = 0, medianLifespan = 0;
    double reproductiveSuccess = 0; // share of (dead, non-founder) organisms with ≥1 offspring
    double offspringPerOrganism = 0;
    double totalBirths = 0;
    double maxGeneration = 0;
    double finalDiversity = 0, meanDiversity = 0;
    double meanFoodFraction = 0;
    double energyCapture = 0;       // assimilated plant energy / primary production
    double msPerTick = 0;
    double finalClusters = 0;
    std::vector<double> finalTraitMeans;   // kNumTraits (NaN if extinct)
    std::vector<std::vector<double>> series;  // rows: tick, population, prey, predators, diversity, foodFraction, meanGeneration, traits...
};

class ExperimentRunner {
public:
    explicit ExperimentRunner(ExperimentSpec spec) : spec_(std::move(spec)) {}

    const ExperimentSpec& spec() const { return spec_; }
    std::size_t runCount() const { return spec_.conditions.size() * spec_.seeds.size(); }

    RunResult runOne(std::size_t condition, std::uint64_t seed) const;

    // Runs everything; `jobs` > 1 uses threads (native only). Progress callback
    // receives (completed, total). Results are ordered by condition then seed.
    std::vector<RunResult> runAll(int jobs, const std::function<void(std::size_t, std::size_t)>& progress = {}) const;

    // Per-condition summary statistics + aggregated time series.
    Json summarize(const std::vector<RunResult>& results) const;
    static Json runToJson(const RunResult& r);

    // Writes runs.csv, timeseries.csv, timeseries_summary.csv, summary.csv, summary.md, meta.json.
    void writeOutputs(const std::string& dir, const std::vector<RunResult>& results, double wallSeconds) const;

    static std::vector<std::string> seriesColumns();
    static std::vector<std::string> runMetricNames();
    static std::vector<double> runMetricValues(const RunResult& r);

private:
    ExperimentSpec spec_;
};

}  // namespace evo
