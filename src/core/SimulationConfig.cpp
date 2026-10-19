#include "core/SimulationConfig.hpp"

#include <cmath>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

#include "organisms/Genome.hpp"

namespace evo {

std::string readTextFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw ConfigError("cannot open file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

namespace {

// One visitor type reads JSON into the struct, the other writes the struct to
// JSON, so the list of fields lives in exactly one place.
struct Reader {
    const Json* obj;
    std::string prefix;
    std::vector<std::string>* errors;
    std::set<std::string> known;

    Reader(const Json* o, std::string p, std::vector<std::string>* e) : obj(o), prefix(std::move(p)), errors(e) {
        if (obj && !obj->isObject()) {
            errors->push_back(prefix + ": expected an object");
            obj = nullptr;
        }
    }

    const Json* get(const char* key) {
        known.insert(key);
        if (!obj || !obj->contains(key)) return nullptr;
        return &obj->at(key);
    }
    void err(const char* key, const std::string& msg) { errors->push_back(prefix + key + ": " + msg); }

    void operator()(const char* key, double& v) {
        if (auto* j = get(key)) {
            if (j->isNumber() && std::isfinite(j->asNumber())) v = j->asNumber();
            else err(key, "expected a number");
        }
    }
    void operator()(const char* key, int& v) {
        if (auto* j = get(key)) {
            if (j->isNumber() && std::floor(j->asNumber()) == j->asNumber() && std::fabs(j->asNumber()) < 2e9)
                v = static_cast<int>(j->asNumber());
            else err(key, "expected an integer");
        }
    }
    void operator()(const char* key, std::uint64_t& v) {
        if (auto* j = get(key)) {
            if (j->isNumber() && j->asNumber() >= 0 && std::floor(j->asNumber()) == j->asNumber() && j->asNumber() < 9.007e15)
                v = static_cast<std::uint64_t>(j->asNumber());
            else err(key, "expected a non-negative integer below 2^53");
        }
    }
    void operator()(const char* key, bool& v) {
        if (auto* j = get(key)) {
            if (j->isBool()) v = j->asBool();
            else err(key, "expected true/false");
        }
    }
    void operator()(const char* key, std::string& v) {
        if (auto* j = get(key)) {
            if (j->isString()) v = j->asString();
            else err(key, "expected a string");
        }
    }
    void finish() {
        if (!obj) return;
        for (const auto& [k, _] : obj->asObject())
            if (!known.count(k)) errors->push_back(prefix + k + ": unknown key");
    }
};

struct Writer {
    Json obj = Json::object();
    void operator()(const char* key, double v) { obj[key] = Json(v); }
    void operator()(const char* key, int v) { obj[key] = Json(v); }
    void operator()(const char* key, std::uint64_t v) { obj[key] = Json(v); }
    void operator()(const char* key, bool v) { obj[key] = Json(v); }
    void operator()(const char* key, const std::string& v) { obj[key] = Json(v); }
};

template <class V, class C> void visitWorld(V& v, C& c) { v("width", c.width); v("height", c.height); }
template <class V, class C> void visitPopulation(V& v, C& c) {
    v("initialPrey", c.initialPrey);
    v("maxPopulation", c.maxPopulation);
    v("initialEnergyFrac", c.initialEnergyFrac);
}
template <class V, class C> void visitResources(V& v, C& c) {
    v("cellSize", c.cellSize); v("distribution", c.distribution); v("capacity", c.capacity);
    v("regrowthRate", c.regrowthRate); v("seedRate", c.seedRate); v("patchCount", c.patchCount);
    v("patchRadius", c.patchRadius); v("backgroundFraction", c.backgroundFraction);
    v("shiftPeriod", c.shiftPeriod); v("seasonAmplitude", c.seasonAmplitude);
    v("seasonPeriod", c.seasonPeriod); v("initialFill", c.initialFill);
}
template <class V, class C> void visitOrganisms(V& v, C& c) {
    v("basalRate", c.basalRate); v("moveCost", c.moveCost); v("visionCost", c.visionCost);
    v("energyPerSize", c.energyPerSize); v("biteRate", c.biteRate); v("thermalCost", c.thermalCost);
    v("lifespanCost", c.lifespanCost); v("senescenceRate", c.senescenceRate);
    v("contactRadius", c.contactRadius); v("attentionLimit", c.attentionLimit);
    v("contestWeight", c.contestWeight);
}
template <class V, class C> void visitReproduction(V& v, C& c) {
    v("enabled", c.enabled); v("mode", c.mode); v("thresholdFrac", c.thresholdFrac);
    v("overheadFrac", c.overheadFrac); v("cooldown", c.cooldown);
    v("compatibilityThreshold", c.compatibilityThreshold);
}
template <class V, class C> void visitMutation(V& v, C& c) {
    v("mode", c.mode); v("fixedRate", c.fixedRate); v("scale", c.scale);
    v("largeProb", c.largeProb); v("largeScale", c.largeScale);
}
template <class V, class C> void visitController(V& v, C& c) { v("type", c.type); v("neuralHidden", c.neuralHidden); }
template <class V, class C> void visitPredators(V& v, C& c) {
    v("enabled", c.enabled); v("initialCount", c.initialCount); v("introduceAtTick", c.introduceAtTick);
    v("captureBase", c.captureBase); v("huntCost", c.huntCost); v("groupDefense", c.groupDefense);
    v("sizeDefense", c.sizeDefense); v("basalMultiplier", c.basalMultiplier); v("minVision", c.minVision);
    v("meatAssimilation", c.meatAssimilation); v("handlingTime", c.handlingTime);
    v("interference", c.interference); v("strikeRange", c.strikeRange);
    v("storageMultiplier", c.storageMultiplier);
}
template <class V, class C> void visitEnvironment(V& v, C& c) {
    v("temperatureGradient", c.temperatureGradient); v("temperatureOffset", c.temperatureOffset);
    v("cycleAmplitude", c.cycleAmplitude); v("cyclePeriod", c.cyclePeriod);
}
template <class V, class C> void visitAnalytics(V& v, C& c) {
    v("sampleInterval", c.sampleInterval); v("clusterInterval", c.clusterInterval);
    v("clusterThreshold", c.clusterThreshold); v("diversitySample", c.diversitySample);
    v("maxLineageRecords", c.maxLineageRecords); v("maxSamples", c.maxSamples);
}

template <class Fn, class C>
void readSection(const Json& root, const char* key, C& c, std::vector<std::string>& errors, Fn visit,
                 std::set<std::string>* extraKeys = nullptr,
                 const std::function<void(const Json&, std::vector<std::string>&)>& extra = {}) {
    const Json* obj = root.contains(key) ? &root.at(key) : nullptr;
    Reader r(obj, std::string(key) + ".", &errors);
    visit(r, c);
    if (extraKeys) for (const auto& k : *extraKeys) r.known.insert(k);
    if (obj && obj->isObject() && extra) extra(*obj, errors);
    r.finish();
}

std::map<std::string, std::pair<double, double>> readTraitRanges(const Json& j, const std::string& where,
                                                                 std::vector<std::string>& errors) {
    std::map<std::string, std::pair<double, double>> out;
    if (!j.isObject()) { errors.push_back(where + ": expected an object of gene -> [min, max]"); return out; }
    for (const auto& [gene, range] : j.asObject()) {
        if (geneIndex(gene) < 0) { errors.push_back(where + "." + gene + ": unknown gene"); continue; }
        if (!range.isArray() || range.size() != 2 || !range.asArray()[0].isNumber() || !range.asArray()[1].isNumber()) {
            errors.push_back(where + "." + gene + ": expected [min, max]");
            continue;
        }
        out[gene] = {range.asArray()[0].asNumber(), range.asArray()[1].asNumber()};
    }
    return out;
}

Json writeTraitRanges(const std::map<std::string, std::pair<double, double>>& m) {
    Json o = Json::object();
    for (const auto& [g, r] : m) o[g] = Json(Json::Array{Json(r.first), Json(r.second)});
    return o;
}

}  // namespace

SimulationConfig SimulationConfig::fromJson(const Json& j) {
    if (!j.isObject()) throw ConfigError("config must be a JSON object");
    SimulationConfig c;
    std::vector<std::string> errors;

    std::set<std::string> topKnown = {"version", "name", "seed", "world", "population", "resources",
                                      "organisms", "reproduction", "mutation", "controller",
                                      "predators", "environment", "analytics", "engineVersion"};
    for (const auto& [k, _] : j.asObject())
        if (!topKnown.count(k)) errors.push_back(k + ": unknown key");

    if (j.contains("version")) {
        const Json& v = j.at("version");
        if (!v.isNumber()) errors.push_back("version: expected an integer");
        else if (static_cast<int>(v.asNumber()) != kConfigVersion)
            errors.push_back("version: config format v" + formatNumber(v.asNumber()) +
                             " is not supported by this engine (expects v" + std::to_string(kConfigVersion) + ")");
    }
    {
        Reader r(&j, "", &errors);
        r("name", c.name);
        r("seed", c.seed);
    }

    readSection(j, "world", c.world, errors, [](auto& v, auto& s) { visitWorld(v, s); });

    std::set<std::string> popExtra = {"initialTraits", "lockedGenes", "groups"};
    readSection(j, "population", c.population, errors, [](auto& v, auto& s) { visitPopulation(v, s); }, &popExtra,
                [&](const Json& o, std::vector<std::string>& errs) {
                    if (o.contains("initialTraits"))
                        c.population.initialTraits = readTraitRanges(o.at("initialTraits"), "population.initialTraits", errs);
                    if (o.contains("lockedGenes")) {
                        const Json& lg = o.at("lockedGenes");
                        if (!lg.isArray()) errs.push_back("population.lockedGenes: expected an array of gene names");
                        else for (const auto& g : lg.asArray()) {
                            if (!g.isString() || geneIndex(g.asString()) < 0)
                                errs.push_back("population.lockedGenes: unknown gene " + g.dump());
                            else c.population.lockedGenes.push_back(g.asString());
                        }
                    }
                    if (o.contains("groups")) {
                        const Json& gs = o.at("groups");
                        if (!gs.isArray()) { errs.push_back("population.groups: expected an array"); return; }
                        for (std::size_t i = 0; i < gs.size(); ++i) {
                            const Json& g = gs.asArray()[i];
                            std::string where = "population.groups[" + std::to_string(i) + "]";
                            if (!g.isObject()) { errs.push_back(where + ": expected an object"); continue; }
                            PopulationConfig::Group grp;
                            Reader r(&g, where + ".", &errs);
                            r("name", grp.name);
                            r("fraction", grp.fraction);
                            r.known.insert("traits");
                            if (g.contains("traits")) grp.traits = readTraitRanges(g.at("traits"), where + ".traits", errs);
                            r.finish();
                            c.population.groups.push_back(grp);
                        }
                    }
                });

    readSection(j, "resources", c.resources, errors, [](auto& v, auto& s) { visitResources(v, s); });
    readSection(j, "organisms", c.organisms, errors, [](auto& v, auto& s) { visitOrganisms(v, s); });
    readSection(j, "reproduction", c.reproduction, errors, [](auto& v, auto& s) { visitReproduction(v, s); });
    readSection(j, "mutation", c.mutation, errors, [](auto& v, auto& s) { visitMutation(v, s); });
    readSection(j, "controller", c.controller, errors, [](auto& v, auto& s) { visitController(v, s); });
    std::set<std::string> predExtra = {"initialTraits"};
    readSection(j, "predators", c.predators, errors, [](auto& v, auto& s) { visitPredators(v, s); }, &predExtra,
                [&](const Json& o, std::vector<std::string>& errs) {
                    if (o.contains("initialTraits"))
                        c.predators.initialTraits = readTraitRanges(o.at("initialTraits"), "predators.initialTraits", errs);
                });

    std::set<std::string> envExtra = {"hazards", "events"};
    readSection(j, "environment", c.environment, errors, [](auto& v, auto& s) { visitEnvironment(v, s); }, &envExtra,
                [&](const Json& o, std::vector<std::string>& errs) {
                    if (o.contains("hazards")) {
                        const Json& hs = o.at("hazards");
                        if (!hs.isArray()) errs.push_back("environment.hazards: expected an array");
                        else for (std::size_t i = 0; i < hs.size(); ++i) {
                            Hazard h;
                            Reader r(&hs.asArray()[i], "environment.hazards[" + std::to_string(i) + "].", &errs);
                            r("x", h.center.x); r("y", h.center.y); r("radius", h.radius); r("damage", h.damage);
                            r.finish();
                            c.environment.hazards.push_back(h);
                        }
                    }
                    if (o.contains("events")) {
                        const Json& es = o.at("events");
                        if (!es.isArray()) errs.push_back("environment.events: expected an array");
                        else for (std::size_t i = 0; i < es.size(); ++i) {
                            EnvEvent e;
                            Reader r(&es.asArray()[i], "environment.events[" + std::to_string(i) + "].", &errs);
                            r("tick", e.tick); r("type", e.type); r("value", e.value);
                            r("x", e.pos.x); r("y", e.pos.y); r("radius", e.radius);
                            r("damage", e.damage); r("count", e.count);
                            r.finish();
                            c.environment.events.push_back(e);
                        }
                    }
                });
    readSection(j, "analytics", c.analytics, errors, [](auto& v, auto& s) { visitAnalytics(v, s); });

    // Range checks run even when some keys failed to parse: such fields keep
    // their (valid) defaults, so every reported problem is a real one.
    {
        auto problems = c.validate();
        errors.insert(errors.end(), problems.begin(), problems.end());
    }
    if (!errors.empty()) {
        std::string msg = "invalid configuration:";
        for (const auto& e : errors) msg += "\n  - " + e;
        throw ConfigError(msg);
    }
    return c;
}

SimulationConfig SimulationConfig::fromJsonText(const std::string& text) {
    Json j;
    try {
        j = Json::parse(text);
    } catch (const JsonError& e) {
        throw ConfigError(e.what());
    }
    return fromJson(j);
}

SimulationConfig SimulationConfig::loadFile(const std::string& path) { return fromJsonText(readTextFile(path)); }

Json SimulationConfig::toJson() const {
    Json j = Json::object();
    j["version"] = Json(version);
    j["engineVersion"] = Json(kEngineVersion);
    j["name"] = Json(name);
    j["seed"] = Json(seed);
    auto section = [&](const char* key, auto fn) { Writer w; fn(w); j[key] = std::move(w.obj); };
    section("world", [&](Writer& w) { visitWorld(w, world); });
    section("population", [&](Writer& w) { visitPopulation(w, population); });
    if (!population.initialTraits.empty()) j["population"]["initialTraits"] = writeTraitRanges(population.initialTraits);
    if (!population.lockedGenes.empty()) {
        Json a = Json::array();
        for (const auto& g : population.lockedGenes) a.push(Json(g));
        j["population"]["lockedGenes"] = a;
    }
    if (!population.groups.empty()) {
        Json a = Json::array();
        for (const auto& g : population.groups) {
            Json o = Json::object();
            o["name"] = Json(g.name);
            o["fraction"] = Json(g.fraction);
            o["traits"] = writeTraitRanges(g.traits);
            a.push(o);
        }
        j["population"]["groups"] = a;
    }
    section("resources", [&](Writer& w) { visitResources(w, resources); });
    section("organisms", [&](Writer& w) { visitOrganisms(w, organisms); });
    section("reproduction", [&](Writer& w) { visitReproduction(w, reproduction); });
    section("mutation", [&](Writer& w) { visitMutation(w, mutation); });
    section("controller", [&](Writer& w) { visitController(w, controller); });
    section("predators", [&](Writer& w) { visitPredators(w, predators); });
    j["predators"]["initialTraits"] = writeTraitRanges(predators.initialTraits);
    section("environment", [&](Writer& w) { visitEnvironment(w, environment); });
    {
        Json hs = Json::array();
        for (const auto& h : environment.hazards) {
            Json o = Json::object();
            o["x"] = Json(h.center.x); o["y"] = Json(h.center.y);
            o["radius"] = Json(h.radius); o["damage"] = Json(h.damage);
            hs.push(o);
        }
        j["environment"]["hazards"] = hs;
        Json es = Json::array();
        for (const auto& e : environment.events) {
            Json o = Json::object();
            o["tick"] = Json(e.tick); o["type"] = Json(e.type); o["value"] = Json(e.value);
            o["x"] = Json(e.pos.x); o["y"] = Json(e.pos.y); o["radius"] = Json(e.radius);
            o["damage"] = Json(e.damage); o["count"] = Json(e.count);
            es.push(o);
        }
        j["environment"]["events"] = es;
    }
    section("analytics", [&](Writer& w) { visitAnalytics(w, analytics); });
    return j;
}

std::vector<std::string> SimulationConfig::validate() const {
    std::vector<std::string> p;
    auto need = [&](bool ok, const std::string& msg) { if (!ok) p.push_back(msg); };
    need(world.width >= 20 && world.width <= 20000, "world.width must be in [20, 20000]");
    need(world.height >= 20 && world.height <= 20000, "world.height must be in [20, 20000]");
    need(population.initialPrey >= 0 && population.initialPrey <= 200000, "population.initialPrey must be in [0, 200000]");
    need(population.maxPopulation >= 1 && population.maxPopulation <= 500000, "population.maxPopulation must be in [1, 500000]");
    need(population.initialEnergyFrac > 0 && population.initialEnergyFrac <= 1, "population.initialEnergyFrac must be in (0, 1]");
    for (const auto& [g, r] : population.initialTraits) {
        const auto& spec = geneSpec(geneIndex(g));
        need(r.first <= r.second, "population.initialTraits." + g + ": min > max");
        need(r.first >= spec.min && r.second <= spec.max,
             "population.initialTraits." + g + ": must lie within gene range [" + formatNumber(spec.min) + ", " + formatNumber(spec.max) + "]");
    }
    double groupSum = 0;
    for (const auto& grp : population.groups) {
        need(grp.fraction > 0, "population.groups: fraction must be > 0");
        groupSum += grp.fraction;
        for (const auto& [g, r] : grp.traits) {
            const auto& spec = geneSpec(geneIndex(g));
            need(r.first <= r.second && r.first >= spec.min && r.second <= spec.max,
                 "population.groups." + grp.name + "." + g + ": range invalid or outside gene range");
        }
    }
    need(population.groups.empty() || groupSum > 0, "population.groups: fractions must sum to > 0");
    need(resources.cellSize >= 1 && resources.cellSize <= 100, "resources.cellSize must be in [1, 100]");
    need(resources.distribution == "uniform" || resources.distribution == "clustered" || resources.distribution == "shifting",
         "resources.distribution must be uniform | clustered | shifting");
    need(resources.capacity > 0, "resources.capacity must be > 0");
    need(resources.regrowthRate >= 0 && resources.regrowthRate <= 1, "resources.regrowthRate must be in [0, 1]");
    need(resources.seedRate >= 0 && resources.seedRate <= 1, "resources.seedRate must be in [0, 1]");
    need(resources.patchCount >= 0 && resources.patchCount <= 1000, "resources.patchCount must be in [0, 1000]");
    need(resources.patchRadius > 0, "resources.patchRadius must be > 0");
    need(resources.backgroundFraction >= 0 && resources.backgroundFraction <= 1, "resources.backgroundFraction must be in [0, 1]");
    need(resources.shiftPeriod >= 1, "resources.shiftPeriod must be >= 1");
    need(resources.seasonAmplitude >= 0 && resources.seasonAmplitude < 1, "resources.seasonAmplitude must be in [0, 1)");
    need(resources.seasonPeriod >= 1, "resources.seasonPeriod must be >= 1");
    need(resources.initialFill >= 0 && resources.initialFill <= 1, "resources.initialFill must be in [0, 1]");
    need(organisms.basalRate >= 0 && organisms.moveCost >= 0 && organisms.visionCost >= 0 &&
         organisms.thermalCost >= 0 && organisms.lifespanCost >= 0, "organisms costs must be >= 0");
    need(organisms.energyPerSize > 0, "organisms.energyPerSize must be > 0");
    need(organisms.biteRate > 0, "organisms.biteRate must be > 0");
    need(organisms.senescenceRate >= 0 && organisms.senescenceRate <= 1, "organisms.senescenceRate must be in [0, 1]");
    need(organisms.contactRadius > 0, "organisms.contactRadius must be > 0");
    need(organisms.contestWeight >= 0 && organisms.contestWeight <= 4, "organisms.contestWeight must be in [0, 4]");
    need(organisms.attentionLimit >= 1 && organisms.attentionLimit <= 1000, "organisms.attentionLimit must be in [1, 1000]");
    need(reproduction.mode == "asexual" || reproduction.mode == "sexual", "reproduction.mode must be asexual | sexual");
    need(reproduction.thresholdFrac > 0 && reproduction.thresholdFrac <= 1, "reproduction.thresholdFrac must be in (0, 1]");
    need(reproduction.overheadFrac >= 0 && reproduction.overheadFrac <= 2, "reproduction.overheadFrac must be in [0, 2]");
    need(reproduction.cooldown >= 0, "reproduction.cooldown must be >= 0");
    need(reproduction.compatibilityThreshold > 0, "reproduction.compatibilityThreshold must be > 0");
    need(mutation.mode == "evolvable" || mutation.mode == "fixed", "mutation.mode must be evolvable | fixed");
    need(mutation.fixedRate >= 0 && mutation.fixedRate <= 1, "mutation.fixedRate must be in [0, 1]");
    need(mutation.scale >= 0 && mutation.scale <= 1, "mutation.scale must be in [0, 1]");
    need(mutation.largeProb >= 0 && mutation.largeProb <= 1, "mutation.largeProb must be in [0, 1]");
    need(mutation.largeScale >= 0 && mutation.largeScale <= 1, "mutation.largeScale must be in [0, 1]");
    need(controller.type == "utility" || controller.type == "neural", "controller.type must be utility | neural");
    need(controller.neuralHidden >= 1 && controller.neuralHidden <= 64, "controller.neuralHidden must be in [1, 64]");
    need(predators.initialCount >= 0, "predators.initialCount must be >= 0");
    need(predators.introduceAtTick >= 0, "predators.introduceAtTick must be >= 0");
    need(predators.captureBase >= 0 && predators.captureBase <= 1, "predators.captureBase must be in [0, 1]");
    need(predators.huntCost >= 0 && predators.groupDefense >= 0 && predators.sizeDefense >= 0 && predators.basalMultiplier >= 0,
         "predators costs/defenses must be >= 0");
    need(predators.storageMultiplier > 0 && predators.storageMultiplier <= 20, "predators.storageMultiplier must be in (0, 20]");
    need(predators.strikeRange >= 0 && predators.strikeRange <= 50, "predators.strikeRange must be in [0, 50]");
    need(predators.interference >= 0, "predators.interference must be >= 0");
    need(predators.handlingTime >= 0, "predators.handlingTime must be >= 0");
    need(predators.meatAssimilation > 0 && predators.meatAssimilation <= 5, "predators.meatAssimilation must be in (0, 5]");
    for (const auto& [g, r] : predators.initialTraits) {
        const auto& spec = geneSpec(geneIndex(g));
        need(r.first <= r.second && r.first >= spec.min && r.second <= spec.max,
             "predators.initialTraits." + g + ": range invalid or outside gene range");
    }
    need(environment.cyclePeriod >= 1, "environment.cyclePeriod must be >= 1");
    for (const auto& h : environment.hazards)
        need(h.radius > 0 && h.damage >= 0, "environment.hazards: radius must be > 0 and damage >= 0");
    static const std::set<std::string> evTypes = {"foodMultiplier", "temperatureShift", "addHazard", "clearHazards",
                                                  "shiftResources", "introducePredators", "setRegrowth", "removeRandom",
                                                  "depleteRegion"};
    for (const auto& e : environment.events) {
        need(evTypes.count(e.type) > 0, "environment.events: unknown type '" + e.type + "'");
        need(e.tick >= 0, "environment.events: tick must be >= 0");
    }
    need(analytics.sampleInterval >= 1, "analytics.sampleInterval must be >= 1");
    need(analytics.clusterInterval >= 1, "analytics.clusterInterval must be >= 1");
    need(analytics.clusterThreshold > 0, "analytics.clusterThreshold must be > 0");
    need(analytics.diversitySample >= 2, "analytics.diversitySample must be >= 2");
    need(analytics.maxSamples >= 16, "analytics.maxSamples must be >= 16");
    return p;
}

}  // namespace evo
