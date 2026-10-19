// evosim — command-line front door to the engine (no rendering).
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "analytics/ExperimentRunner.hpp"
#include "core/Simulation.hpp"
#include "serialization/Checkpoint.hpp"

using namespace evo;

namespace {

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::map<std::string, std::string> opts;
    bool has(const std::string& k) const { return opts.count(k) > 0; }
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = opts.find(k);
        return it == opts.end() ? def : it->second;
    }
};

Args parseArgs(int argc, char** argv) {
    Args a;
    if (argc > 1) a.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            std::string key = s.substr(2), val = "1";
            auto eq = key.find('=');
            if (eq != std::string::npos) { val = key.substr(eq + 1); key = key.substr(0, eq); }
            else if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) val = argv[++i];
            a.opts[key] = val;
        } else {
            a.positional.push_back(s);
        }
    }
    return a;
}

void usage() {
    std::cout <<
R"(evosim — EvoLab evolution simulator (engine )" << kEngineVersion << R"()

USAGE
  evosim run        [--config FILE] [--ticks N] [--seed S] [--csv FILE] [--print-every N]
                    [--checkpoint FILE] [--resume FILE] [--set path=value ...]
  evosim experiment SPEC.json [--out DIR] [--jobs N] [--seeds N] [--ticks N]
  evosim bench      [--config FILE] [--sizes 500,1000,2000] [--ticks N]
  evosim hash       [--config FILE] [--ticks N] [--seed S]    print state hash (determinism checks)
  evosim validate   FILE                                       validate a config file
  evosim genes                                                 list genes, ranges, effects
  evosim default-config                                        print the built-in default config

Overrides: --set accepts dotted paths, e.g. --set mutation.fixedRate=0.1 (repeat via comma:
  --set "mutation.mode=fixed,mutation.fixedRate=0.1").
)";
}

Json parseValue(const std::string& v) {
    try {
        return Json::parse(v);
    } catch (...) {
        return Json(v);  // bare strings
    }
}

SimulationConfig loadConfig(const Args& a) {
    Json j = a.has("config") ? Json::parse(readTextFile(a.get("config"))) : SimulationConfig{}.toJson();
    if (a.has("set")) {
        std::stringstream ss(a.get("set"));
        std::string item;
        while (std::getline(ss, item, ',')) {
            auto eq = item.find('=');
            if (eq == std::string::npos) throw ConfigError("--set expects path=value, got '" + item + "'");
            j.setPath(item.substr(0, eq), parseValue(item.substr(eq + 1)));
        }
    }
    if (a.has("seed")) j["seed"] = Json(static_cast<std::uint64_t>(std::stoull(a.get("seed"))));
    return SimulationConfig::fromJson(j);
}

void printProgress(const Simulation& sim) {
    const Sample s = sim.makeSample();
    std::printf("tick %7llu | pop %5d (prey %5d, pred %4d) | gen %5.1f max %4u | div %.3f | food %.2f | "
                "speed %.2f vision %.1f size %.2f maturity %5.0f\n",
                static_cast<unsigned long long>(s.tick), s.population, s.prey, s.predators, s.meanGeneration,
                s.maxGeneration, s.diversity, s.foodFraction, s.traitMean[Speed], s.traitMean[Vision],
                s.traitMean[BodySize], s.traitMean[Maturity]);
    std::fflush(stdout);
}

int cmdRun(const Args& a) {
    std::unique_ptr<Simulation> sim;
    if (a.has("resume")) {
        sim = Checkpoint::loadFile(a.get("resume"));
        std::printf("resumed from %s at tick %llu\n", a.get("resume").c_str(), static_cast<unsigned long long>(sim->tick()));
    } else {
        sim = std::make_unique<Simulation>(loadConfig(a));
    }
    const std::uint64_t ticks = std::stoull(a.get("ticks", "2000"));
    const std::uint64_t every = std::stoull(a.get("print-every", "500"));
    const std::uint64_t target = sim->tick() + ticks;
    std::printf("seed %llu, %zu organisms, controller=%s, reproduction=%s\n",
                static_cast<unsigned long long>(sim->config().seed), sim->organisms().size(), sim->controller().name(),
                sim->config().reproduction.mode.c_str());
    printProgress(*sim);
    const auto t0 = std::chrono::steady_clock::now();
    while (sim->tick() < target) {
        sim->step();
        if (every && sim->tick() % every == 0) printProgress(*sim);
        if (sim->organisms().empty()) {
            std::printf("population extinct at tick %llu\n", static_cast<unsigned long long>(sim->tick()));
            break;
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("ran %llu ticks in %.2fs (%.3f ms/tick); births %llu, deaths %llu; state hash %016llx\n",
                static_cast<unsigned long long>(ticks), secs, 1000.0 * secs / std::max<std::uint64_t>(1, ticks),
                static_cast<unsigned long long>(sim->stats().totalBirths()),
                static_cast<unsigned long long>(sim->stats().totalDeaths()),
                static_cast<unsigned long long>(sim->stateHash()));
    if (a.has("csv")) {
        std::ofstream f(a.get("csv"));
        f << Statistics::csvHeader() << '\n';
        for (const auto& s : sim->stats().samples()) f << Statistics::csvRow(s) << '\n';
        std::printf("wrote %zu samples to %s\n", sim->stats().samples().size(), a.get("csv").c_str());
    }
    if (a.has("checkpoint")) {
        Checkpoint::saveFile(*sim, a.get("checkpoint"));
        std::printf("wrote checkpoint %s\n", a.get("checkpoint").c_str());
    }
    return 0;
}

int cmdExperiment(const Args& a) {
    if (a.positional.empty()) throw std::runtime_error("experiment: missing SPEC.json");
    Json j = Json::parse(readTextFile(a.positional[0]));
    if (a.has("seeds")) j["seeds"] = Json(std::stoi(a.get("seeds")));
    if (a.has("ticks")) j["ticks"] = Json(std::stoi(a.get("ticks")));
    ExperimentSpec spec = ExperimentSpec::fromJson(j, std::filesystem::path(a.positional[0]).parent_path().string());
    ExperimentRunner runner(spec);
    const int jobs = std::stoi(a.get("jobs", "1"));
    const std::string out = a.get("out", "results/" + spec.name);
    std::printf("experiment '%s': %zu conditions × %zu seeds × %llu ticks, %d job(s)\n", spec.name.c_str(),
                spec.conditions.size(), spec.seeds.size(), static_cast<unsigned long long>(spec.ticks), jobs);
    const auto t0 = std::chrono::steady_clock::now();
    auto results = runner.runAll(jobs, [](std::size_t d, std::size_t n) {
        std::printf("\r  runs completed: %zu/%zu", d, n);
        std::fflush(stdout);
    });
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n  finished in %.1fs\n", secs);
    runner.writeOutputs(out, results, secs);
    std::printf("  wrote %s/{runs.csv,timeseries.csv,timeseries_summary.csv,summary.csv,summary.json,summary.md,meta.json}\n",
                out.c_str());
    std::ifstream md(std::filesystem::path(out) / "summary.md");
    std::cout << '\n' << md.rdbuf() << '\n';
    return 0;
}

int cmdBench(const Args& a) {
    SimulationConfig base = loadConfig(a);
    std::vector<int> sizes;
    std::stringstream ss(a.get("sizes", "500,1000,2000,5000,10000,20000"));
    std::string item;
    while (std::getline(ss, item, ',')) sizes.push_back(std::stoi(item));
    const int ticks = std::stoi(a.get("ticks", "200"));
    std::printf("%8s %10s %10s %12s %10s   phase breakdown (%% of tick)\n", "target", "pop@end", "ms/tick", "ticks/s", "world");
    for (int n : sizes) {
        SimulationConfig cfg = base;
        // Keep organism density constant: scale world area with population.
        const double density = static_cast<double>(base.population.initialPrey) / (base.world.width * base.world.height);
        const double area = n / density;
        cfg.world.width = std::sqrt(area * 1.5);
        cfg.world.height = area / cfg.world.width;
        cfg.population.initialPrey = n;
        cfg.population.maxPopulation = n;
        cfg.analytics.sampleInterval = 1000000;
        cfg.analytics.clusterInterval = 1000000;
        Simulation sim(cfg);
        sim.setProfiling(true);
        sim.run(20);  // warm-up
        sim.resetProfile();
        const auto t0 = std::chrono::steady_clock::now();
        sim.run(static_cast<std::uint64_t>(ticks));
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double ms = 1000.0 * secs / ticks;
        std::printf("%8d %10zu %10.3f %12.1f %5.0fx%-5.0f  ", n, sim.organisms().size(), ms, ticks / secs,
                    cfg.world.width, cfg.world.height);
        const auto& prof = sim.profile();
        double total = 0;
        for (double v : prof.ms) total += v;
        for (std::size_t k = 0; k < prof.ms.size(); ++k)
            std::printf("%s %.0f%%  ", Simulation::kPhaseNames[k], total > 0 ? 100.0 * prof.ms[k] / total : 0.0);
        std::printf("\n");
        std::fflush(stdout);
    }
    return 0;
}

int cmdHash(const Args& a) {
    Simulation sim(loadConfig(a));
    const std::uint64_t ticks = std::stoull(a.get("ticks", "1000"));
    const std::uint64_t every = std::stoull(a.get("every", "0"));
    for (std::uint64_t t = 0; t < ticks; ++t) {
        sim.step();
        if (every && sim.tick() % every == 0)
            std::printf("%llu %016llx\n", static_cast<unsigned long long>(sim.tick()), static_cast<unsigned long long>(sim.stateHash()));
    }
    std::printf("%016llx\n", static_cast<unsigned long long>(sim.stateHash()));
    return 0;
}

int cmdGenes() {
    std::printf("%-15s %8s %8s   %-14s %s\n", "gene", "min", "max", "founder range", "effect");
    for (int g = 0; g < kNumTraits; ++g) {
        const auto& s = geneSpec(g);
        std::printf("%-15s %8.3g %8.3g   [%5.3g, %5.3g]  %s\n", s.name, s.min, s.max, s.initMin, s.initMax, s.description);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Args a = parseArgs(argc, argv);
    try {
        if (a.command == "run") return cmdRun(a);
        if (a.command == "experiment") return cmdExperiment(a);
        if (a.command == "bench") return cmdBench(a);
        if (a.command == "hash") return cmdHash(a);
        if (a.command == "genes") return cmdGenes();
        if (a.command == "dump") {
            Simulation sim(loadConfig(a));
            sim.run(std::stoull(a.get("ticks", "0")));
            std::cout << sim.debugDump().dump() << '\n';
            return 0;
        }
        if (a.command == "default-config") { std::cout << SimulationConfig{}.toJson().dump(2) << '\n'; return 0; }
        if (a.command == "validate") {
            if (a.positional.empty()) throw std::runtime_error("validate: missing FILE");
            SimulationConfig::loadFile(a.positional[0]);
            std::printf("%s: valid (config v%d)\n", a.positional[0].c_str(), kConfigVersion);
            return 0;
        }
        usage();
        return a.command.empty() || a.command == "help" || a.command == "--help" ? 0 : 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
