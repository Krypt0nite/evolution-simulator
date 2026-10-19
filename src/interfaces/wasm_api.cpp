// C ABI exported to JavaScript via Emscripten. This is the ONLY surface the
// browser sees: it sends commands and reads snapshots; it never decides
// anything about organisms.
//
// Conventions
//   * Functions returning `const char*` return a pointer to a UTF-8 JSON string
//     owned by this module; it stays valid until the next call that returns a
//     string. JS must copy it (UTF8ToString) immediately.
//   * Buffer functions return a pointer into wasm memory plus a separate count;
//     JS must create typed-array views *after* the call (memory may grow).
//   * Errors: functions return 0/-1 and evo_last_error() explains why.
#include <emscripten/emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "analytics/ExperimentRunner.hpp"
#include "core/Simulation.hpp"
#include "serialization/Checkpoint.hpp"

using namespace evo;

namespace {
std::unique_ptr<Simulation> g_sim;
std::string g_str;
std::string g_error;
std::vector<float> g_render;
std::vector<std::uint32_t> g_renderIds;
std::vector<std::uint8_t> g_food;
std::vector<float> g_traitValues;
std::vector<std::uint8_t> g_bytes;

std::unique_ptr<ExperimentRunner> g_exp;
std::vector<std::pair<std::size_t, std::uint64_t>> g_expTasks;
std::vector<RunResult> g_expResults;
std::size_t g_expNext = 0;

constexpr int kRenderStride = 11;  // x, y, hx, hy, size, energyFrac, kind, cluster, vision, action, generation

const char* ret(std::string s) {
    g_str = std::move(s);
    return g_str.c_str();
}

template <typename Fn>
int guarded(Fn&& fn) {
    try {
        return fn();
    } catch (const std::exception& e) {
        g_error = e.what();
        return -1;
    }
}

Json vecJson(Vec2 v) { return Json(Json::Array{Json(v.x), Json(v.y)}); }

Json organismJson(const Simulation& s, const Organism& o, std::size_t index) {
    Json j = Json::object();
    j["id"] = Json(o.id);
    j["kind"] = Json(o.kind == Kind::Prey ? "prey" : "predator");
    j["parentA"] = Json(o.parentA);
    j["parentB"] = Json(o.parentB);
    j["founder"] = Json(o.founder);
    j["generation"] = Json(static_cast<std::uint64_t>(o.generation));
    j["age"] = Json(static_cast<std::uint64_t>(o.age));
    j["energy"] = Json(o.energy);
    j["maxEnergy"] = Json(o.pheno.maxEnergy);
    j["position"] = vecJson(o.pos);
    j["heading"] = vecJson(o.heading);
    j["cooldown"] = Json(static_cast<std::uint64_t>(o.cooldown));
    j["offspring"] = Json(static_cast<std::uint64_t>(o.offspring));
    j["cluster"] = Json(o.cluster);
    j["mature"] = Json(o.mature());
    j["action"] = Json(actionName(o.action));
    Json recent = Json::array();
    for (std::size_t k = 0; k < o.recent.size(); ++k) {
        // Oldest → newest.
        const std::size_t idx = (o.recentHead + k) % o.recent.size();
        recent.push(Json(actionName(o.recent[idx])));
    }
    j["recentActions"] = recent;
    Json genome = Json::object();
    for (int g = 0; g < kNumTraits; ++g) genome[geneSpec(g).name] = Json(o.genome[g]);
    j["genome"] = genome;
    j["brainWeights"] = Json(static_cast<std::uint64_t>(o.genome.brainSize()));
    const Phenotype& p = o.pheno;
    Json ph = Json::object();
    ph["maxSpeed"] = Json(p.maxSpeed); ph["vision"] = Json(p.vision); ph["size"] = Json(p.size);
    ph["maxEnergy"] = Json(p.maxEnergy); ph["biteRate"] = Json(p.biteRate); ph["assimilation"] = Json(p.assimilation);
    ph["basalCost"] = Json(p.basalCost); ph["moveCoeff"] = Json(p.moveCoeff); ph["thermalCoeff"] = Json(p.thermalCoeff);
    ph["maturity"] = Json(p.maturity); ph["lifespan"] = Json(p.lifespan); ph["fertility"] = Json(p.fertility);
    ph["investment"] = Json(p.investment); ph["reproThreshold"] = Json(p.reproThreshold);
    ph["temperatureHere"] = Json(s.environment().temperature(o.pos));
    j["phenotype"] = ph;

    const Perception per = s.perceive(index);
    Json pj = Json::object();
    pj["hunger"] = Json(per.hunger);
    pj["foodSeen"] = Json(per.foodSeen);
    if (per.foodSeen) { pj["foodDist"] = Json(per.foodDist); pj["foodAmount"] = Json(per.foodAmount); }
    pj["threatSeen"] = Json(per.threatSeen);
    if (per.threatSeen) pj["threatDist"] = Json(per.threatDist);
    pj["preySeen"] = Json(per.preySeen);
    if (per.preySeen) pj["preyDist"] = Json(per.preyDist);
    pj["mateSeen"] = Json(per.mateSeen);
    pj["hazardSeen"] = Json(per.hazardSeen);
    pj["neighbours"] = Json(per.neighbours);
    pj["tempMismatch"] = Json(per.tempMismatch);
    pj["readyToMate"] = Json(per.readyToMate);
    j["perception"] = pj;

    const auto& L = s.lineage();
    Json anc = Json::array();
    for (auto a : L.ancestors(o.id, 16)) anc.push(Json(a));
    j["ancestors"] = anc;
    Json kids = Json::array();
    for (auto c : L.children(o.id, 24)) {
        Json kj = Json::object();
        kj["id"] = Json(c);
        const auto* r = L.get(c);
        kj["alive"] = Json(r && r->deathTick == UINT32_MAX);
        kids.push(kj);
    }
    j["children"] = kids;
    j["descendants"] = Json(L.descendantCount(o.id));
    std::uint64_t lineageAlive = 0;
    for (const auto& x : s.organisms()) lineageAlive += x.founder == o.founder;
    j["lineageAlive"] = Json(lineageAlive);
    return j;
}

Json clusterJson(const Simulation& s) {
    Json arr = Json::array();
    int k = 0;
    for (const auto& c : s.species().clusters()) {
        if (++k > 24) break;
        Json cj = Json::object();
        cj["id"] = Json(c.id);
        cj["size"] = Json(c.size);
        cj["prey"] = Json(c.preySize);
        cj["predators"] = Json(c.predatorSize);
        Json cen = Json::object();
        for (int g = 0; g < kNumTraits; ++g) cen[geneSpec(g).name] = Json(c.centroid[static_cast<std::size_t>(g)]);
        cj["centroid"] = cen;
        arr.push(cj);
    }
    return arr;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* evo_version() { return kEngineVersion; }
EMSCRIPTEN_KEEPALIVE const char* evo_last_error() { return g_error.c_str(); }

EMSCRIPTEN_KEEPALIVE const char* evo_default_config() { return ret(SimulationConfig{}.toJson().dump(2)); }

EMSCRIPTEN_KEEPALIVE const char* evo_genes() {
    Json arr = Json::array();
    for (int g = 0; g < kNumTraits; ++g) {
        const auto& s = geneSpec(g);
        Json j = Json::object();
        j["name"] = Json(s.name); j["min"] = Json(s.min); j["max"] = Json(s.max);
        j["initMin"] = Json(s.initMin); j["initMax"] = Json(s.initMax); j["description"] = Json(s.description);
        arr.push(j);
    }
    return ret(arr.dump());
}

// Validates a config without creating a simulation. Returns 1 if valid.
EMSCRIPTEN_KEEPALIVE int evo_validate_config(const char* json) {
    return guarded([&] { SimulationConfig::fromJsonText(json); return 1; });
}

EMSCRIPTEN_KEEPALIVE int evo_create(const char* json) {
    return guarded([&] {
        auto sim = std::make_unique<Simulation>(SimulationConfig::fromJsonText(json));
        g_sim = std::move(sim);
        return 1;
    });
}

EMSCRIPTEN_KEEPALIVE const char* evo_config() { return ret(g_sim ? g_sim->config().toJson().dump(2) : "null"); }

EMSCRIPTEN_KEEPALIVE int evo_step(int n) {
    if (!g_sim) return -1;
    return guarded([&] {
        for (int i = 0; i < n; ++i) {
            g_sim->step();
            if (g_sim->organisms().empty()) break;
        }
        return static_cast<int>(g_sim->organisms().size());
    });
}

EMSCRIPTEN_KEEPALIVE double evo_tick() { return g_sim ? static_cast<double>(g_sim->tick()) : 0.0; }
EMSCRIPTEN_KEEPALIVE int evo_count() { return g_sim ? static_cast<int>(g_sim->organisms().size()) : 0; }
EMSCRIPTEN_KEEPALIVE int evo_render_stride() { return kRenderStride; }
EMSCRIPTEN_KEEPALIVE const char* evo_hash() {
    if (!g_sim) return "";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(g_sim->stateHash()));
    return ret(buf);
}

EMSCRIPTEN_KEEPALIVE const char* evo_debug_dump() { return ret(g_sim ? g_sim->debugDump().dump() : "null"); }

// Packs render data for every organism: see kRenderStride.
EMSCRIPTEN_KEEPALIVE float* evo_render_buffer() {
    if (!g_sim) return nullptr;
    const auto& orgs = g_sim->organisms();
    g_render.resize(orgs.size() * kRenderStride);
    g_renderIds.resize(orgs.size() * 2);
    for (std::size_t i = 0; i < orgs.size(); ++i) {
        const auto& o = orgs[i];
        float* r = &g_render[i * kRenderStride];
        r[0] = static_cast<float>(o.pos.x);
        r[1] = static_cast<float>(o.pos.y);
        r[2] = static_cast<float>(o.heading.x);
        r[3] = static_cast<float>(o.heading.y);
        r[4] = static_cast<float>(o.pheno.size);
        r[5] = static_cast<float>(o.energy / o.pheno.maxEnergy);
        r[6] = o.kind == Kind::Prey ? 0.f : 1.f;
        r[7] = static_cast<float>(o.cluster);
        r[8] = static_cast<float>(o.pheno.vision);
        r[9] = static_cast<float>(static_cast<int>(o.action));
        r[10] = static_cast<float>(o.generation);
        g_renderIds[i * 2] = static_cast<std::uint32_t>(o.id);
        g_renderIds[i * 2 + 1] = static_cast<std::uint32_t>(o.founder);
    }
    return g_render.data();
}
EMSCRIPTEN_KEEPALIVE std::uint32_t* evo_render_ids() { return g_renderIds.data(); }

// Values of one gene for every living organism (same order as the render
// buffer), for trait-distribution histograms.
EMSCRIPTEN_KEEPALIVE float* evo_trait_values(int gene) {
    if (!g_sim || gene < 0 || gene >= kNumTraits) return nullptr;
    const auto& orgs = g_sim->organisms();
    g_traitValues.resize(orgs.size());
    for (std::size_t i = 0; i < orgs.size(); ++i) g_traitValues[i] = static_cast<float>(orgs[i].genome[gene]);
    return g_traitValues.data();
}

// Food density 0..255 relative to the configured capacity.
EMSCRIPTEN_KEEPALIVE std::uint8_t* evo_food_buffer() {
    if (!g_sim) return nullptr;
    const auto& f = g_sim->food();
    const auto& b = f.biomassData();
    g_food.resize(b.size());
    const double inv = 255.0 / f.maxCapacity();
    for (std::size_t i = 0; i < b.size(); ++i) g_food[i] = static_cast<std::uint8_t>(std::min(255.0, b[i] * inv + 0.5));
    return g_food.data();
}

EMSCRIPTEN_KEEPALIVE const char* evo_world() {
    if (!g_sim) return ret("null");
    const auto& c = g_sim->config();
    const auto& env = g_sim->environment();
    Json j = Json::object();
    j["width"] = Json(c.world.width);
    j["height"] = Json(c.world.height);
    j["foodCols"] = Json(g_sim->food().cols());
    j["foodRows"] = Json(g_sim->food().rows());
    j["foodCell"] = Json(g_sim->food().cellSize());
    j["foodMultiplier"] = Json(g_sim->food().multiplier());
    j["regrowthRate"] = Json(g_sim->food().regrowthRate());
    j["temperatureOffset"] = Json(env.offset());
    j["temperatureCycle"] = Json(env.cycleValue());
    j["temperatureGradient"] = Json(env.gradient());
    Json hz = Json::array();
    for (const auto& h : env.hazards()) {
        Json o = Json::object();
        o["x"] = Json(h.center.x); o["y"] = Json(h.center.y); o["radius"] = Json(h.radius); o["damage"] = Json(h.damage);
        hz.push(o);
    }
    j["hazards"] = hz;
    j["seed"] = Json(c.seed);
    j["tick"] = Json(g_sim->tick());
    j["controller"] = Json(g_sim->controller().name());
    j["reproduction"] = Json(c.reproduction.mode);
    j["predatorsEnabled"] = Json(c.predators.enabled);
    j["engineVersion"] = Json(kEngineVersion);
    j["lineageTruncated"] = Json(g_sim->lineage().truncated());
    return ret(j.dump());
}

// Statistics samples from index `from` onward, plus totals, ledger, clusters.
EMSCRIPTEN_KEEPALIVE const char* evo_stats(int from) {
    if (!g_sim) return ret("null");
    const auto& samples = g_sim->stats().samples();
    Json j = Json::object();
    j["total"] = Json(static_cast<std::uint64_t>(samples.size()));
    Json arr = Json::array();
    for (std::size_t i = static_cast<std::size_t>(std::max(0, from)); i < samples.size(); ++i) arr.push(samples[i].toJson());
    j["samples"] = arr;
    j["current"] = g_sim->makeSample().toJson();
    j["totalBirths"] = Json(g_sim->stats().totalBirths());
    j["totalDeaths"] = Json(g_sim->stats().totalDeaths());
    j["ledger"] = g_sim->ledger().toJson();
    j["clusters"] = clusterJson(*g_sim);
    // Lineages (founders) ranked by living descendants.
    std::vector<std::pair<std::uint64_t, int>> counts;
    {
        std::vector<std::uint64_t> f;
        for (const auto& o : g_sim->organisms()) f.push_back(o.founder);
        std::sort(f.begin(), f.end());
        for (std::size_t i = 0; i < f.size();) {
            std::size_t k = i;
            while (k < f.size() && f[k] == f[i]) ++k;
            counts.push_back({f[i], static_cast<int>(k - i)});
            i = k;
        }
        std::stable_sort(counts.begin(), counts.end(), [](auto& a, auto& b) { return a.second > b.second; });
    }
    Json lin = Json::array();
    for (std::size_t i = 0; i < counts.size() && i < 12; ++i) {
        Json o = Json::object();
        o["founder"] = Json(counts[i].first);
        o["alive"] = Json(counts[i].second);
        lin.push(o);
    }
    j["lineages"] = lin;
    return ret(j.dump());
}

// Nearest organism to (x, y) within `radius`, or 0.
EMSCRIPTEN_KEEPALIVE double evo_pick(double x, double y, double radius) {
    if (!g_sim) return 0;
    double best = radius * radius;
    std::uint64_t id = 0;
    for (const auto& o : g_sim->organisms()) {
        const double d = distanceSq(o.pos, {x, y});
        if (d <= best) { best = d; id = o.id; }
    }
    return static_cast<double>(id);
}

EMSCRIPTEN_KEEPALIVE const char* evo_inspect(double idD) {
    if (!g_sim) return ret("null");
    const auto id = static_cast<std::uint64_t>(idD);
    const auto& orgs = g_sim->organisms();
    auto it = std::lower_bound(orgs.begin(), orgs.end(), id, [](const Organism& o, std::uint64_t v) { return o.id < v; });
    if (it == orgs.end() || it->id != id) {
        // Dead or unknown: report the lineage record if there is one.
        const auto* r = g_sim->lineage().get(id);
        if (!r) return ret("null");
        Json j = Json::object();
        j["id"] = Json(id);
        j["alive"] = Json(false);
        j["parentA"] = Json(r->parentA);
        j["parentB"] = Json(r->parentB);
        j["founder"] = Json(r->founder);
        j["generation"] = Json(static_cast<std::uint64_t>(r->generation));
        j["birthTick"] = Json(static_cast<std::uint64_t>(r->birthTick));
        j["deathTick"] = Json(static_cast<std::uint64_t>(r->deathTick));
        j["cause"] = Json(deathCauseName(static_cast<DeathCause>(r->cause)));
        j["offspring"] = Json(static_cast<std::uint64_t>(r->offspring));
        j["descendants"] = Json(g_sim->lineage().descendantCount(id));
        return ret(j.dump());
    }
    Json j = organismJson(*g_sim, *it, static_cast<std::size_t>(it - orgs.begin()));
    j["alive"] = Json(true);
    if (const auto* r = g_sim->lineage().get(id)) j["birthTick"] = Json(static_cast<std::uint64_t>(r->birthTick));
    return ret(j.dump());
}

// Schedules an environment event (JSON object, same shape as config events) for the next tick.
EMSCRIPTEN_KEEPALIVE int evo_schedule_event(const char* json) {
    if (!g_sim) return -1;
    return guarded([&] {
        Json cfg = g_sim->config().toJson();
        Json ev = Json::parse(json);
        ev["tick"] = Json(static_cast<double>(g_sim->tick()));
        // Validate through the config parser so the browser gets the same error messages.
        Json probe = cfg;
        probe["environment"]["events"] = Json(Json::Array{ev});
        const SimulationConfig parsed = SimulationConfig::fromJson(probe);
        g_sim->scheduleEvent(parsed.environment.events.front());
        return 1;
    });
}

EMSCRIPTEN_KEEPALIVE int evo_checkpoint_save() {
    if (!g_sim) return -1;
    return guarded([&] {
        g_bytes = Checkpoint::save(*g_sim);
        return static_cast<int>(g_bytes.size());
    });
}
EMSCRIPTEN_KEEPALIVE std::uint8_t* evo_checkpoint_ptr() { return g_bytes.data(); }

EMSCRIPTEN_KEEPALIVE int evo_checkpoint_load(const std::uint8_t* data, int len) {
    return guarded([&] {
        std::vector<std::uint8_t> bytes(data, data + len);
        g_sim = Checkpoint::load(bytes);
        return 1;
    });
}

// ---- Batch experiments (run one simulation per evo_exp_next call so a Web
// ---- Worker can report progress between runs).
EMSCRIPTEN_KEEPALIVE int evo_exp_begin(const char* specJson) {
    return guarded([&] {
        ExperimentSpec spec = ExperimentSpec::fromJson(Json::parse(specJson), "");
        g_exp = std::make_unique<ExperimentRunner>(spec);
        g_expTasks.clear();
        g_expResults.clear();
        g_expNext = 0;
        for (std::size_t c = 0; c < spec.conditions.size(); ++c)
            for (auto s : spec.seeds) g_expTasks.push_back({c, s});
        return static_cast<int>(g_expTasks.size());
    });
}

EMSCRIPTEN_KEEPALIVE int evo_exp_next() {
    if (!g_exp) return -1;
    return guarded([&] {
        if (g_expNext < g_expTasks.size()) {
            const auto [c, s] = g_expTasks[g_expNext++];
            g_expResults.push_back(g_exp->runOne(c, s));
        }
        return static_cast<int>(g_expNext);
    });
}

EMSCRIPTEN_KEEPALIVE const char* evo_exp_summary() {
    if (!g_exp) return ret("null");
    try {
        Json j = g_exp->summarize(g_expResults);
        Json runs = Json::array();
        for (const auto& r : g_expResults) runs.push(ExperimentRunner::runToJson(r));
        j["runs"] = runs;
        j["completed"] = Json(static_cast<std::uint64_t>(g_expResults.size()));
        j["total"] = Json(static_cast<std::uint64_t>(g_expTasks.size()));
        return ret(j.dump());
    } catch (const std::exception& e) {
        g_error = e.what();
        return ret("null");
    }
}

}  // extern "C"

int main() { return 0; }
