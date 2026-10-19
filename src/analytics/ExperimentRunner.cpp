#include "analytics/ExperimentRunner.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#ifndef __EMSCRIPTEN__
#include <thread>
#endif

#include "analytics/StatsMath.hpp"
#include "core/Simulation.hpp"

namespace evo {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::string csvNum(double v) { return std::isfinite(v) ? formatNumber(v) : ""; }

std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}
}  // namespace

// ---------------------------------------------------------------- spec

ExperimentSpec ExperimentSpec::fromJson(const Json& j, const std::string& baseDir) {
    ExperimentSpec s;
    auto fail = [](const std::string& m) { throw ConfigError("experiment: " + m); };
    if (!j.isObject()) fail("spec must be an object");
    static const char* known[] = {"name", "description", "hypothesis", "base", "baseConfig", "ticks", "seeds",
                                  "seedStart", "sampleInterval", "recoveryAfterTick", "conditions"};
    for (const auto& [k, _] : j.asObject()) {
        bool ok = false;
        for (const char* kk : known) ok |= k == kk;
        if (!ok) fail("unknown key '" + k + "'");
    }
    s.name = j.contains("name") ? j.at("name").asString() : "experiment";
    if (j.contains("description")) s.description = j.at("description").asString();
    if (j.contains("hypothesis")) s.hypothesis = j.at("hypothesis").asString();
    if (j.contains("base")) {
        s.base = j.at("base");
    } else if (j.contains("baseConfig")) {
        std::filesystem::path p = j.at("baseConfig").asString();
        if (p.is_relative() && !baseDir.empty()) p = std::filesystem::path(baseDir) / p;
        try {
            s.base = Json::parse(readTextFile(p.string()));
        } catch (const JsonError& e) {
            fail(std::string("base config ") + p.string() + ": " + e.what());
        }
    }
    if (j.contains("ticks")) s.ticks = static_cast<std::uint64_t>(j.at("ticks").asNumber());
    if (j.contains("sampleInterval")) s.sampleInterval = static_cast<int>(j.at("sampleInterval").asNumber());
    if (j.contains("recoveryAfterTick")) s.recoveryAfterTick = static_cast<int>(j.at("recoveryAfterTick").asNumber());
    const std::uint64_t seedStart = j.contains("seedStart") ? static_cast<std::uint64_t>(j.at("seedStart").asNumber()) : 1;
    if (j.contains("seeds")) {
        const Json& sj = j.at("seeds");
        if (sj.isNumber()) {
            for (int i = 0; i < static_cast<int>(sj.asNumber()); ++i) s.seeds.push_back(seedStart + static_cast<std::uint64_t>(i));
        } else {
            for (const auto& v : sj.asArray()) s.seeds.push_back(static_cast<std::uint64_t>(v.asNumber()));
        }
    } else {
        for (int i = 0; i < 8; ++i) s.seeds.push_back(seedStart + static_cast<std::uint64_t>(i));
    }
    if (s.seeds.empty()) fail("needs at least one seed");
    if (s.ticks < 1) fail("ticks must be >= 1");
    if (s.sampleInterval < 1) fail("sampleInterval must be >= 1");
    if (!j.contains("conditions") || !j.at("conditions").isArray() || j.at("conditions").size() == 0)
        fail("needs a non-empty 'conditions' array");
    for (const auto& c : j.at("conditions").asArray()) {
        ExperimentCondition cond;
        cond.name = c.contains("name") ? c.at("name").asString() : "condition";
        if (c.contains("overrides")) cond.overrides = c.at("overrides");
        if (!cond.overrides.isObject()) fail("condition '" + cond.name + "': overrides must be an object");
        s.conditions.push_back(cond);
    }
    // Validate every condition up front so a typo fails before hours of compute.
    for (std::size_t i = 0; i < s.conditions.size(); ++i) {
        try {
            (void)s.configFor(i, s.seeds.front());
        } catch (const std::exception& e) {
            fail("condition '" + s.conditions[i].name + "': " + e.what());
        }
    }
    return s;
}

ExperimentSpec ExperimentSpec::loadFile(const std::string& path) {
    Json j;
    try {
        j = Json::parse(readTextFile(path));
    } catch (const JsonError& e) {
        throw ConfigError(path + ": " + e.what());
    }
    return fromJson(j, std::filesystem::path(path).parent_path().string());
}

Json ExperimentSpec::toJson() const {
    Json j = Json::object();
    j["name"] = Json(name);
    j["description"] = Json(description);
    j["hypothesis"] = Json(hypothesis);
    j["ticks"] = Json(ticks);
    j["sampleInterval"] = Json(sampleInterval);
    j["recoveryAfterTick"] = Json(recoveryAfterTick);
    Json seedsJ = Json::array();
    for (auto s : seeds) seedsJ.push(Json(s));
    j["seeds"] = seedsJ;
    j["base"] = base;
    Json conds = Json::array();
    for (const auto& c : conditions) {
        Json o = Json::object();
        o["name"] = Json(c.name);
        o["overrides"] = c.overrides;
        conds.push(o);
    }
    j["conditions"] = conds;
    return j;
}

SimulationConfig ExperimentSpec::configFor(std::size_t condition, std::uint64_t seed) const {
    Json cfg = base;
    if (cfg.isNull()) cfg = Json::object();
    for (const auto& [path, value] : conditions.at(condition).overrides.asObject()) cfg.setPath(path, value);
    cfg["seed"] = Json(seed);
    cfg["analytics"]["sampleInterval"] = Json(sampleInterval);
    if (!cfg.contains("name")) cfg["name"] = Json(name + "/" + conditions.at(condition).name);
    return SimulationConfig::fromJson(cfg);
}

// ---------------------------------------------------------------- running

std::vector<std::string> ExperimentRunner::seriesColumns() {
    std::vector<std::string> c = {"tick", "population", "prey", "predators", "diversity", "food_fraction", "mean_generation"};
    for (int g = 0; g < kNumTraits; ++g) c.push_back(std::string("mean_") + geneSpec(g).name);
    return c;
}

RunResult ExperimentRunner::runOne(std::size_t condition, std::uint64_t seed) const {
    const SimulationConfig cfg = spec_.configFor(condition, seed);
    RunResult r;
    r.condition = spec_.conditions[condition].name;
    r.conditionIndex = condition;
    r.seed = seed;
    const auto t0 = std::chrono::steady_clock::now();
    Simulation sim(cfg);
    bool predatorsSeen = sim.predatorCount() > 0;
    while (sim.tick() < spec_.ticks) {
        sim.step();
        if (!predatorsSeen && sim.predatorCount() > 0) predatorsSeen = true;
        if (sim.organisms().empty()) {
            r.extinct = true;
            r.extinctionTick = sim.tick();
            break;
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.ticksRun = sim.tick();
    r.msPerTick = r.ticksRun ? 1000.0 * secs / r.ticksRun : 0.0;
    r.extinct = r.extinct || sim.preyCount() == 0;
    if (r.extinct && r.extinctionTick == 0) r.extinctionTick = sim.tick();
    r.predatorsExtinct = predatorsSeen && sim.predatorCount() == 0;
    r.finalPopulation = sim.organisms().size();
    r.finalPrey = sim.preyCount();
    r.finalPredators = sim.predatorCount();

    // Time series; extinct runs are padded with zeros so every run has the same rows.
    const auto& samples = sim.stats().samples();
    const std::size_t traitCols = kNumTraits;
    for (const auto& s : samples) {
        std::vector<double> row = {static_cast<double>(s.tick), static_cast<double>(s.population),
                                   static_cast<double>(s.prey), static_cast<double>(s.predators),
                                   s.prey > 1 ? s.diversity : kNaN, s.foodFraction, s.meanGeneration};
        for (std::size_t g = 0; g < traitCols; ++g) row.push_back(s.prey > 0 ? s.traitMean[g] : kNaN);
        r.series.push_back(std::move(row));
    }
    std::uint64_t lastTick = samples.empty() ? 0 : samples.back().tick;
    for (std::uint64_t t = lastTick + static_cast<std::uint64_t>(spec_.sampleInterval); t <= spec_.ticks;
         t += static_cast<std::uint64_t>(spec_.sampleInterval)) {
        std::vector<double> row = {static_cast<double>(t), 0, 0, 0, kNaN, kNaN, kNaN};
        for (std::size_t g = 0; g < traitCols; ++g) row.push_back(kNaN);
        r.series.push_back(std::move(row));
    }

    // Population summaries.
    std::vector<double> secondHalf;
    r.minPopulation = std::numeric_limits<double>::max();
    std::vector<double> div;
    double foodSum = 0;
    int foodN = 0;
    for (const auto& row : r.series) {
        if (row[0] >= spec_.ticks / 2.0) secondHalf.push_back(row[1]);
        if (row[0] > 0) r.minPopulation = std::min(r.minPopulation, row[1]);
        if (std::isfinite(row[4])) div.push_back(row[4]);
        if (std::isfinite(row[5])) { foodSum += row[5]; ++foodN; }
    }
    if (r.minPopulation == std::numeric_limits<double>::max()) r.minPopulation = r.finalPopulation;
    r.meanPopulation = stats::mean(secondHalf);
    r.meanDiversity = div.empty() ? kNaN : stats::mean(div);
    r.finalDiversity = !samples.empty() && samples.back().prey > 1 ? samples.back().diversity : kNaN;
    r.meanFoodFraction = foodN ? foodSum / foodN : kNaN;
    r.maxGeneration = samples.empty() ? 0 : samples.back().maxGeneration;
    r.finalClusters = sim.species().clusters().size();

    // Recovery after a perturbation: ticks until population is back to 90% of
    // its pre-perturbation mean, measured from the perturbation tick.
    r.recoveryTicks = kNaN;
    if (spec_.recoveryAfterTick >= 0) {
        std::vector<double> pre;
        for (const auto& row : r.series)
            if (row[0] < spec_.recoveryAfterTick && row[0] >= spec_.recoveryAfterTick - 1000.0) pre.push_back(row[1]);
        const double target = 0.9 * stats::mean(pre);
        bool dipped = false;
        for (const auto& row : r.series) {
            if (row[0] <= spec_.recoveryAfterTick) continue;
            if (row[1] < target) dipped = true;
            else if (dipped) { r.recoveryTicks = row[0] - spec_.recoveryAfterTick; break; }
        }
        if (!dipped && std::isfinite(target)) r.recoveryTicks = 0;
    }

    // Lifetime statistics from the lineage record (non-founders that died).
    std::vector<double> lifespans;
    double offspringSum = 0, reproduced = 0;
    for (const auto& rec : sim.lineage().records()) {
        if (rec.parentA == 0 || rec.deathTick == UINT32_MAX || rec.kind != 0) continue;
        lifespans.push_back(static_cast<double>(rec.deathTick - rec.birthTick));
        offspringSum += rec.offspring;
        reproduced += rec.offspring > 0 ? 1 : 0;
    }
    r.meanLifespan = stats::mean(lifespans);
    r.medianLifespan = stats::median(lifespans);
    r.offspringPerOrganism = lifespans.empty() ? kNaN : offspringSum / lifespans.size();
    r.reproductiveSuccess = lifespans.empty() ? kNaN : reproduced / lifespans.size();
    r.totalBirths = static_cast<double>(sim.stats().totalBirths());
    // Share of primary production (plant regrowth) that consumers assimilated.
    const auto& L = sim.ledger();
    r.energyCapture = L.inflow > 0 ? L.assimilatedPlants / L.inflow : kNaN;

    r.finalTraitMeans.assign(kNumTraits, kNaN);
    if (!samples.empty() && samples.back().prey > 0 && !r.extinct)
        for (int g = 0; g < kNumTraits; ++g) r.finalTraitMeans[static_cast<std::size_t>(g)] = samples.back().traitMean[static_cast<std::size_t>(g)];
    return r;
}

std::vector<RunResult> ExperimentRunner::runAll(int jobs, const std::function<void(std::size_t, std::size_t)>& progress) const {
    struct Task { std::size_t cond; std::uint64_t seed; };
    std::vector<Task> tasks;
    for (std::size_t c = 0; c < spec_.conditions.size(); ++c)
        for (auto s : spec_.seeds) tasks.push_back({c, s});
    std::vector<RunResult> results(tasks.size());
    std::atomic<std::size_t> next{0}, done{0};
    std::mutex progressMutex;
    std::vector<std::string> errors;
    auto worker = [&]() {
        while (true) {
            const std::size_t k = next++;
            if (k >= tasks.size()) return;
            try {
                results[k] = runOne(tasks[k].cond, tasks[k].seed);
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(progressMutex);
                errors.push_back(e.what());
            }
            const std::size_t d = ++done;
            if (progress) {
                std::lock_guard<std::mutex> lock(progressMutex);
                progress(d, tasks.size());
            }
        }
    };
#ifdef __EMSCRIPTEN__
    (void)jobs;
    worker();
#else
    const int n = std::max(1, std::min<int>(jobs, static_cast<int>(tasks.size())));
    if (n == 1) {
        worker();
    } else {
        std::vector<std::thread> threads;
        for (int i = 0; i < n; ++i) threads.emplace_back(worker);
        for (auto& t : threads) t.join();
    }
#endif
    if (!errors.empty()) throw std::runtime_error("experiment run failed: " + errors.front());
    return results;
}

// ---------------------------------------------------------------- summaries

std::vector<std::string> ExperimentRunner::runMetricNames() {
    std::vector<std::string> n = {"extinct", "final_population", "mean_population", "min_population",
                                  "final_predators", "predators_extinct", "mean_lifespan", "median_lifespan",
                                  "reproductive_success", "offspring_per_organism", "total_births",
                                  "max_generation", "final_diversity", "mean_diversity", "mean_food_fraction",
                                  "energy_capture", "recovery_ticks", "final_clusters", "ms_per_tick"};
    for (int g = 0; g < kNumTraits; ++g) n.push_back(std::string("final_") + geneSpec(g).name);
    return n;
}

std::vector<double> ExperimentRunner::runMetricValues(const RunResult& r) {
    std::vector<double> v = {r.extinct ? 1.0 : 0.0, r.finalPopulation, r.meanPopulation, r.minPopulation,
                             r.finalPredators, r.predatorsExtinct ? 1.0 : 0.0, r.meanLifespan, r.medianLifespan,
                             r.reproductiveSuccess, r.offspringPerOrganism, r.totalBirths, r.maxGeneration,
                             r.finalDiversity, r.meanDiversity, r.meanFoodFraction, r.energyCapture,
                             r.recoveryTicks, r.finalClusters, r.msPerTick};
    for (double t : r.finalTraitMeans) v.push_back(t);
    return v;
}

Json ExperimentRunner::runToJson(const RunResult& r) {
    Json j = Json::object();
    j["condition"] = Json(r.condition);
    j["seed"] = Json(r.seed);
    j["ticksRun"] = Json(r.ticksRun);
    j["extinctionTick"] = Json(r.extinct ? static_cast<double>(r.extinctionTick) : kNaN);
    const auto names = runMetricNames();
    const auto vals = runMetricValues(r);
    Json m = Json::object();
    for (std::size_t i = 0; i < names.size(); ++i) m[names[i]] = std::isfinite(vals[i]) ? Json(vals[i]) : Json();
    j["metrics"] = m;
    return j;
}

Json ExperimentRunner::summarize(const std::vector<RunResult>& results) const {
    const auto names = runMetricNames();
    const std::size_t nc = spec_.conditions.size();
    // values[c][metric] -> vector over seeds (finite only)
    std::vector<std::vector<std::vector<double>>> values(nc, std::vector<std::vector<double>>(names.size()));
    std::vector<int> extinct(nc, 0), n(nc, 0);
    for (const auto& r : results) {
        const auto v = runMetricValues(r);
        for (std::size_t m = 0; m < names.size(); ++m)
            if (std::isfinite(v[m])) values[r.conditionIndex][m].push_back(v[m]);
        extinct[r.conditionIndex] += r.extinct;
        ++n[r.conditionIndex];
    }
    Json out = Json::object();
    out["experiment"] = Json(spec_.name);
    out["description"] = Json(spec_.description);
    out["hypothesis"] = Json(spec_.hypothesis);
    out["engineVersion"] = Json(kEngineVersion);
    out["ticks"] = Json(spec_.ticks);
    out["seeds"] = Json(static_cast<int>(spec_.seeds.size()));
    out["baseline"] = Json(spec_.conditions.front().name);
    Json conds = Json::array();
    for (std::size_t c = 0; c < nc; ++c) {
        Json cj = Json::object();
        cj["name"] = Json(spec_.conditions[c].name);
        cj["overrides"] = spec_.conditions[c].overrides;
        cj["runs"] = Json(n[c]);
        cj["extinctions"] = Json(extinct[c]);
        const auto w = stats::wilson95(extinct[c], n[c]);
        cj["extinctionCI"] = Json(Json::Array{Json(w.lo), Json(w.hi)});
        Json metrics = Json::object();
        for (std::size_t m = 0; m < names.size(); ++m) {
            const auto& v = values[c][m];
            Json mj = Json::object();
            mj["n"] = Json(static_cast<int>(v.size()));
            auto num = [](double x) { return std::isfinite(x) ? Json(x) : Json(); };
            mj["mean"] = num(stats::mean(v));
            mj["sd"] = num(stats::sd(v));
            mj["median"] = num(stats::median(v));
            const auto ci = stats::meanCI95(v);
            mj["ciLo"] = num(v.size() >= 2 ? ci.lo : kNaN);
            mj["ciHi"] = num(v.size() >= 2 ? ci.hi : kNaN);
            if (c > 0) {
                mj["dVsBaseline"] = num(stats::cohensD(values[0][m], v));
                mj["pVsBaseline"] = num(stats::welchP(values[0][m], v));
            }
            metrics[names[m]] = mj;
        }
        cj["metrics"] = metrics;

        // Aggregated time series (mean and 95% CI across seeds at each sample).
        const auto cols = seriesColumns();
        std::vector<const RunResult*> runs;
        for (const auto& r : results) if (r.conditionIndex == c) runs.push_back(&r);
        std::size_t rows = 0;
        for (auto* r : runs) rows = std::max(rows, r->series.size());
        Json ts = Json::object();
        Json tick = Json::array();
        for (std::size_t row = 0; row < rows; ++row) {
            double t = kNaN;
            for (auto* r : runs) if (row < r->series.size()) { t = r->series[row][0]; break; }
            tick.push(Json(t));
        }
        ts["tick"] = tick;
        for (std::size_t col = 1; col < cols.size(); ++col) {
            Json meanA = Json::array(), lo = Json::array(), hi = Json::array();
            for (std::size_t row = 0; row < rows; ++row) {
                std::vector<double> v;
                for (auto* r : runs)
                    if (row < r->series.size() && std::isfinite(r->series[row][col])) v.push_back(r->series[row][col]);
                const double mu = stats::mean(v);
                const auto ci = stats::meanCI95(v);
                meanA.push(std::isfinite(mu) ? Json(mu) : Json());
                lo.push(v.size() >= 2 && std::isfinite(ci.lo) ? Json(ci.lo) : Json());
                hi.push(v.size() >= 2 && std::isfinite(ci.hi) ? Json(ci.hi) : Json());
            }
            Json colJ = Json::object();
            colJ["mean"] = meanA;
            colJ["lo"] = lo;
            colJ["hi"] = hi;
            ts[cols[col]] = colJ;
        }
        cj["timeseries"] = ts;
        conds.push(cj);
    }
    out["conditions"] = conds;
    return out;
}

void ExperimentRunner::writeOutputs(const std::string& dir, const std::vector<RunResult>& results, double wallSeconds) const {
    namespace fs = std::filesystem;
    fs::create_directories(dir);
    auto open = [&](const std::string& f) {
        std::ofstream o(fs::path(dir) / f);
        if (!o) throw std::runtime_error("cannot write " + (fs::path(dir) / f).string());
        return o;
    };
    const auto names = runMetricNames();
    {
        auto f = open("runs.csv");
        f << "condition,seed,ticks_run,extinction_tick";
        for (const auto& n : names) f << ',' << n;
        f << '\n';
        for (const auto& r : results) {
            f << csvField(r.condition) << ',' << r.seed << ',' << r.ticksRun << ','
              << (r.extinct ? std::to_string(r.extinctionTick) : "");
            for (double v : runMetricValues(r)) f << ',' << csvNum(v);
            f << '\n';
        }
    }
    const auto cols = seriesColumns();
    {
        auto f = open("timeseries.csv");
        f << "condition,seed";
        for (const auto& c : cols) f << ',' << c;
        f << '\n';
        for (const auto& r : results)
            for (const auto& row : r.series) {
                f << csvField(r.condition) << ',' << r.seed;
                for (double v : row) f << ',' << csvNum(v);
                f << '\n';
            }
    }
    const Json summary = summarize(results);
    {
        auto f = open("summary.csv");
        f << "condition,metric,n,mean,sd,median,ci95_lo,ci95_hi,cohens_d_vs_baseline,welch_p_vs_baseline\n";
        for (const auto& cj : summary.at("conditions").asArray()) {
            for (const auto& entry : cj.at("metrics").asObject()) {
                const std::string& metric = entry.first;
                const Json& mj = entry.second;
                auto g = [&](const char* k) {
                    return mj.contains(k) && mj.at(k).isNumber() ? csvNum(mj.at(k).asNumber()) : std::string();
                };
                f << csvField(cj.at("name").asString()) << ',' << metric << ',' << g("n") << ',' << g("mean") << ','
                  << g("sd") << ',' << g("median") << ',' << g("ciLo") << ',' << g("ciHi") << ','
                  << g("dVsBaseline") << ',' << g("pVsBaseline") << '\n';
            }
        }
    }
    {
        auto f = open("timeseries_summary.csv");
        f << "condition,tick,metric,mean,ci95_lo,ci95_hi\n";
        for (const auto& cj : summary.at("conditions").asArray()) {
            const auto& ts = cj.at("timeseries");
            const auto& ticks = ts.at("tick").asArray();
            for (std::size_t col = 1; col < cols.size(); ++col) {
                const auto& cdata = ts.at(cols[col]);
                for (std::size_t i = 0; i < ticks.size(); ++i) {
                    auto g = [&](const char* k) {
                        const auto& a = cdata.at(k).asArray();
                        return i < a.size() && a[i].isNumber() ? csvNum(a[i].asNumber()) : std::string();
                    };
                    f << csvField(cj.at("name").asString()) << ',' << csvNum(ticks[i].asNumber()) << ',' << cols[col]
                      << ',' << g("mean") << ',' << g("lo") << ',' << g("hi") << '\n';
                }
            }
        }
    }
    {
        auto f = open("summary.json");
        f << summary.dump(1);
    }
    {
        auto f = open("spec.json");
        f << spec_.toJson().dump(2);
    }
    {
        Json meta = Json::object();
        meta["engineVersion"] = Json(kEngineVersion);
        meta["configVersion"] = Json(kConfigVersion);
        meta["experiment"] = Json(spec_.name);
        meta["runs"] = Json(static_cast<int>(results.size()));
        meta["wallSeconds"] = Json(wallSeconds);
        const std::time_t now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
        meta["generatedAt"] = Json(std::string(buf));
        auto f = open("meta.json");
        f << meta.dump(2);
    }
    {
        // Human-readable report of the headline metrics.
        auto f = open("summary.md");
        f << "# " << spec_.name << "\n\n";
        if (!spec_.description.empty()) f << spec_.description << "\n\n";
        if (!spec_.hypothesis.empty()) f << "**Question:** " << spec_.hypothesis << "\n\n";
        f << "Runs: " << spec_.seeds.size() << " seeds × " << spec_.conditions.size() << " conditions, "
          << spec_.ticks << " ticks each. Unit of replication = one independent simulation run. "
          << "Values are mean ± SD [95% t-CI]; d = Cohen's d vs. `" << spec_.conditions.front().name
          << "`; p = Welch's t-test.\n\n";
        const std::vector<std::string> headline = {"extinct", "mean_population", "mean_lifespan", "reproductive_success",
                                                   "mean_diversity", "max_generation", "energy_capture"};
        std::vector<std::string> show = headline;
        // Add metrics an experiment is specifically about.
        bool anyPredators = false;
        for (const auto& r : results) anyPredators |= r.finalPredators > 0 || r.predatorsExtinct;
        if (anyPredators) { show.push_back("final_predators"); show.push_back("predators_extinct"); }
        if (spec_.recoveryAfterTick >= 0) show.push_back("recovery_ticks");
        const std::vector<std::string> traits = {"final_speed", "final_vision", "final_size", "final_metabolism",
                                                 "final_maturity", "final_fertility", "final_investment",
                                                 "final_mutationRate", "final_social", "final_riskTolerance",
                                                 "final_thermalOptimum"};
        show.insert(show.end(), traits.begin(), traits.end());
        f << "| metric |";
        for (const auto& cj : summary.at("conditions").asArray()) f << ' ' << cj.at("name").asString() << " |";
        f << "\n|---|";
        for (std::size_t c = 0; c < spec_.conditions.size(); ++c) f << "---|";
        f << '\n';
        for (const auto& m : show) {
            f << "| " << m << " |";
            for (const auto& cj : summary.at("conditions").asArray()) {
                const auto& mj = cj.at("metrics").at(m);
                std::ostringstream cell;
                cell.setf(std::ios::fixed);
                const int prec = std::fabs(mj.at("mean").isNumber() ? mj.at("mean").asNumber() : 0) >= 100 ? 0 : 3;
                cell.precision(prec);
                if (m == "extinct") {
                    const int k = static_cast<int>(cj.at("extinctions").asNumber());
                    const int nn = static_cast<int>(cj.at("runs").asNumber());
                    const auto& ci = cj.at("extinctionCI").asArray();
                    cell.precision(2);
                    cell << k << "/" << nn << " [" << ci[0].asNumber() << ", " << ci[1].asNumber() << "]";
                } else if (!mj.at("mean").isNumber()) {
                    cell << "–";
                } else {
                    cell << mj.at("mean").asNumber();
                    if (mj.at("sd").isNumber()) cell << " ± " << mj.at("sd").asNumber();
                    if (mj.at("ciLo").isNumber()) cell << " [" << mj.at("ciLo").asNumber() << ", " << mj.at("ciHi").asNumber() << "]";
                    if (static_cast<int>(mj.at("n").asNumber()) < static_cast<int>(spec_.seeds.size()))
                        cell << " (n=" << static_cast<int>(mj.at("n").asNumber()) << ")";
                    if (mj.contains("dVsBaseline") && mj.at("dVsBaseline").isNumber()) {
                        cell.precision(2);
                        cell << "; d=" << mj.at("dVsBaseline").asNumber();
                        if (mj.at("pVsBaseline").isNumber()) {
                            const double p = mj.at("pVsBaseline").asNumber();
                            cell.precision(p < 0.001 ? 0 : 3);
                            if (p < 0.001) cell << ", p<0.001";
                            else cell << ", p=" << p;
                        }
                    }
                }
                f << ' ' << cell.str() << " |";
            }
            f << '\n';
        }
        f << "\nRaw per-run data: `runs.csv`; per-sample trajectories: `timeseries.csv`; "
             "aggregated trajectories with 95% CIs: `timeseries_summary.csv`.\n";
    }
}

}  // namespace evo
