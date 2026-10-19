#pragma once
// All tunable parameters. Nothing in the engine hardcodes an ecological
// constant that is not reachable from here.
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/Json.hpp"
#include "core/Vec2.hpp"

namespace evo {

inline constexpr int kConfigVersion = 1;
inline constexpr const char* kEngineVersion = "1.0.0";

struct Hazard {
    Vec2 center;
    double radius = 30.0;
    double damage = 0.5;  // energy drained per tick while inside
};

struct EnvEvent {
    int tick = 0;
    std::string type;  // foodMultiplier | temperatureShift | addHazard | clearHazards |
                       // shiftResources | introducePredators | setRegrowth | removeRandom
    double value = 0.0;
    Vec2 pos;
    double radius = 30.0;
    double damage = 0.5;
    int count = 0;
};

struct WorldConfig {
    double width = 600.0;
    double height = 400.0;
};

struct PopulationConfig {
    int initialPrey = 400;
    int maxPopulation = 20000;       // hard safety cap (births suppressed above it)
    double initialEnergyFrac = 0.6;  // fraction of max energy at t=0
    // Optional per-gene initial ranges, e.g. {"maturity": [40, 60]}.
    std::map<std::string, std::pair<double, double>> initialTraits;
    // Genes that never mutate (for controlled experiments), e.g. ["vision"].
    std::vector<std::string> lockedGenes;
    // Founder mixture: if non-empty, initialPrey is split among these groups,
    // each with its own initialTraits. Used for strategy-competition experiments.
    struct Group {
        std::string name;
        double fraction = 1.0;
        std::map<std::string, std::pair<double, double>> traits;
    };
    std::vector<Group> groups;
};

struct ResourceConfig {
    double cellSize = 5.0;
    std::string distribution = "clustered";  // uniform | clustered | shifting
    double capacity = 16.0;                  // max biomass per cell (energy units)
    double regrowthRate = 0.03;              // logistic growth rate r per tick
    double seedRate = 0.0005;                // spontaneous inflow fraction of K per tick
    int patchCount = 7;
    double patchRadius = 55.0;
    double backgroundFraction = 0.12;        // capacity outside patches (clustered modes)
    int shiftPeriod = 3000;                  // ticks between patch relocations ("shifting")
    double seasonAmplitude = 0.0;            // K *= 1 + A sin(2πt/P)
    int seasonPeriod = 2000;
    double initialFill = 0.8;
};

struct OrganismConfig {
    double basalRate = 0.035;       // base maintenance cost per tick at size 1
    double moveCost = 0.012;        // × size × speed²
    double visionCost = 0.0008;     // per unit vision radius per tick
    double energyPerSize = 60.0;    // energy storage capacity per unit body size
    double biteRate = 1.2;          // biomass per tick at size 1, metabolism 1
    double thermalCost = 0.12;      // × (T - optimum)² × size^0.75
    double lifespanCost = 0.25;     // maintenance overhead of a long lifespan
    double senescenceRate = 0.02;   // death hazard per tick at age == lifespan
    double contactRadius = 1.2;     // contact distance = contactRadius × (size_a + size_b)
    int attentionLimit = 24;        // max neighbours evaluated per perception
    // Interference competition: feeding order is drawn with priority
    // size^contestWeight × U(0.5, 1.5); 0 = size-blind (pure scramble).
    double contestWeight = 1.0;
};

struct ReproductionConfig {
    bool enabled = true;
    std::string mode = "asexual";     // asexual | sexual
    double thresholdFrac = 0.6;       // energy ≥ thresholdFrac × maxEnergy to reproduce
    double overheadFrac = 0.15;       // extra energy lost per unit invested in offspring
    int cooldown = 25;                // ticks between reproduction events
    double compatibilityThreshold = 0.35;  // max genome distance for mating
};

struct MutationConfig {
    std::string mode = "evolvable";  // evolvable (uses mutationRate gene) | fixed
    double fixedRate = 0.05;         // per-gene probability when mode == fixed
    double scale = 0.04;             // small mutation sd, fraction of gene range
    double largeProb = 0.05;         // P(a mutation is large)
    double largeScale = 0.3;         // large mutation sd, fraction of gene range
};

struct ControllerConfig {
    std::string type = "utility";  // utility | neural
    int neuralHidden = 8;
};

struct PredatorConfig {
    bool enabled = false;
    int initialCount = 40;
    int introduceAtTick = 1500;
    double captureBase = 0.45;     // capture probability scale per attempt
    double huntCost = 0.25;        // energy per capture attempt
    double groupDefense = 0.12;    // per nearby social prey: p /= 1 + gd·n·social
    double sizeDefense = 0.6;      // larger prey relative to predator are harder to catch
    double basalMultiplier = 0.7;  // predators' maintenance relative to prey formula
    double minVision = 0.0;        // optional floor for predator vision gene
    double storageMultiplier = 3.0; // predators' energy capacity relative to prey: more kills
                                    // needed per offspring (trophic conversion), longer fasting
    double strikeRange = 2.5;      // extra attack reach beyond body contact
    double interference = 0.3;     // p /= 1 + interference × (other predators within 15 units)
    int handlingTime = 40;         // ticks a predator spends handling a kill (no hunting)
    double meatAssimilation = 1.3; // multiplier on the efficiency gene for prey energy (capped at 0.95)
    // Founder trait ranges for predators (genes not listed use the defaults).
    std::map<std::string, std::pair<double, double>> initialTraits = {{"speed", {1.4, 2.0}}, {"vision", {18.0, 30.0}}};
};

struct EnvironmentConfig {
    double temperatureGradient = 0.6;  // T(y) spans ±gradient from top to bottom
    double temperatureOffset = 0.0;
    double cycleAmplitude = 0.0;       // global sinusoidal temperature swing
    int cyclePeriod = 4000;
    std::vector<Hazard> hazards;
    std::vector<EnvEvent> events;
};

struct AnalyticsConfig {
    int sampleInterval = 50;
    int clusterInterval = 250;
    double clusterThreshold = 0.2;
    int diversitySample = 160;
    int maxLineageRecords = 3000000;
    int maxSamples = 20000;  // time series cap (older samples are thinned 2:1)
};

struct SimulationConfig {
    int version = kConfigVersion;
    std::string name = "default";
    std::uint64_t seed = 42;
    WorldConfig world;
    PopulationConfig population;
    ResourceConfig resources;
    OrganismConfig organisms;
    ReproductionConfig reproduction;
    MutationConfig mutation;
    ControllerConfig controller;
    PredatorConfig predators;
    EnvironmentConfig environment;
    AnalyticsConfig analytics;

    // Throws ConfigError with every problem found.
    static SimulationConfig fromJson(const Json& j);
    static SimulationConfig fromJsonText(const std::string& text);
    static SimulationConfig loadFile(const std::string& path);
    Json toJson() const;

    // Returns human-readable problems; empty == valid.
    std::vector<std::string> validate() const;
};

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::string readTextFile(const std::string& path);

}  // namespace evo
