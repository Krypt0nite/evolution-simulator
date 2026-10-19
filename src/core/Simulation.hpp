#pragma once
// The simulation: owns all state, advances it one fixed tick at a time.
// See docs/ARCHITECTURE.md for the update order.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "analytics/LineageTracker.hpp"
#include "analytics/Speciation.hpp"
#include "analytics/Statistics.hpp"
#include "core/Random.hpp"
#include "core/SimulationConfig.hpp"
#include "core/SpatialGrid.hpp"
#include "ecology/Environment.hpp"
#include "ecology/ResourceField.hpp"
#include "organisms/Brain.hpp"
#include "organisms/Organism.hpp"

namespace evo {

class Simulation {
public:
    explicit Simulation(SimulationConfig cfg);
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    void step();
    void run(std::uint64_t ticks);

    // Queue an environment event for the next tick. It is also appended to the
    // effective config so (config, seed) still reproduces the run.
    void scheduleEvent(EnvEvent e);

    std::uint64_t tick() const { return tick_; }
    const SimulationConfig& config() const { return cfg_; }
    const std::vector<Organism>& organisms() const { return organisms_; }
    const Organism* find(std::uint64_t id) const;
    const ResourceField& food() const { return food_; }
    const Environment& environment() const { return env_; }
    const Statistics& stats() const { return stats_; }
    const LineageTracker& lineage() const { return lineage_; }
    const Speciation& species() const { return species_; }
    const EnergyLedger& ledger() const { return ledger_; }
    const Controller& controller() const { return *controller_; }
    std::uint64_t nextId() const { return nextId_; }
    int preyCount() const;
    int predatorCount() const;
    double organismEnergy() const;

    // Perception exactly as the organism would see it this tick (inspector/tests).
    Perception perceive(std::size_t index) const;

    // Full-precision dump of the dynamic state (for cross-build debugging).
    Json debugDump() const;

    // FNV-1a hash over the full dynamic state (excludes wall-clock timings).
    std::uint64_t stateHash() const;

    // Introduces organisms at random positions with founder genomes.
    void spawnFounders(Kind kind, int count, std::uint64_t forTick);

    Sample makeSample() const;

    // Optional per-phase wall-clock profile (used by `evosim bench`).
    static constexpr std::size_t kPhases = 11;
    static constexpr const char* kPhaseNames[kPhases] = {"events", "index", "sense+decide", "move", "feed", "predation",
                                                         "metabolism", "reproduce", "commit", "regrow", "analytics"};
    struct Profile { std::array<double, kPhases> ms{}; };
    void setProfiling(bool on) { profiling_ = on; }
    void resetProfile() { profile_ = Profile{}; }
    const Profile& profile() const { return profile_; }

    // Mutable access for checkpointing and white-box tests only.
    struct Internals;
    friend struct Internals;
    friend class Checkpoint;

private:
    void applyEvent(const EnvEvent& e);
    void rebuildGrid() const;  // refreshes the (mutable) spatial index
    void phaseDecide();
    void phaseMove();
    void phaseFeed();
    void phasePredation();
    void phaseMetabolism();
    void phaseReproduction();
    void phaseCommit();
    void kill(Organism& o, DeathCause cause);
    double charge(Organism& o, double cost, double& ledgerField);
    Organism makeChild(const Organism& a, const Organism* b, double energy);
    Genome founderGenome(const InitRanges& ranges);
    InitRanges initRangesFor(const std::map<std::string, std::pair<double, double>>& overrides) const;
    void sample();

    SimulationConfig cfg_;
    Rng rng_;
    std::unique_ptr<Controller> controller_;
    std::array<bool, kNumTraits> locked_{};

    std::uint64_t tick_ = 0;
    std::uint64_t nextId_ = 1;
    std::size_t eventCursor_ = 0;
    std::vector<EnvEvent> events_;  // sorted by tick (stable)

    std::vector<Organism> organisms_;
    std::vector<Organism> births_;
    std::vector<Intent> intents_;
    // Spatial index and its scratch buffers are a cache over organisms_; they
    // are rebuilt on demand so const queries (inspector) never see stale indices.
    mutable std::vector<Vec2> positions_;
    mutable std::vector<std::uint8_t> include_;
    mutable SpatialGrid grid_;
    mutable bool gridValid_ = false;
    std::vector<std::uint32_t> order_;
    std::vector<std::pair<double, std::uint32_t>> feedOrder_;
    std::vector<std::uint8_t> claimed_;
    double maxBodySize_ = 3.0;

    ResourceField food_;
    Environment env_;
    LineageTracker lineage_;
    Speciation species_;
    Statistics stats_;
    EnergyLedger ledger_;
    bool profiling_ = false;
    Profile profile_;
    double lastStepMs_ = 0.0;
    double stepMsAccum_ = 0.0;
    int stepMsCount_ = 0;
};

}  // namespace evo
