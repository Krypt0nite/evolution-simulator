#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Json.hpp"
#include "organisms/Genome.hpp"

namespace evo {

// Cumulative energy flows. Invariant (tested):
//   food + organisms  ==  initial + inflow + introduced − Σ losses
struct EnergyLedger {
    double initial = 0.0;
    double inflow = 0.0;        // plant regrowth (the only natural source)
    double introduced = 0.0;    // energy of organisms added by config/events
    double decay = 0.0;         // biomass above a lowered capacity rotting away
    double depleted = 0.0;      // biomass removed by depleteRegion events
    double digestion = 0.0;     // eaten but not assimilated (plants and prey)
    double basal = 0.0;         // maintenance (incl. vision)
    double movement = 0.0;
    double thermal = 0.0;
    double hazard = 0.0;
    double hunting = 0.0;
    double reproduction = 0.0;  // overhead of producing offspring
    double overflow = 0.0;      // offspring endowment above its storage capacity
    double carcass = 0.0;       // energy left in bodies at non-predation death
    double assimilatedPlants = 0.0;  // transfer (not a loss): plants → prey
    double assimilatedPrey = 0.0;    // transfer: prey → predators

    double totalLosses() const {
        return decay + depleted + digestion + basal + movement + thermal + hazard + hunting + reproduction + overflow + carcass;
    }
    Json toJson() const;
};

struct Sample {
    std::uint64_t tick = 0;
    int population = 0, prey = 0, predators = 0;
    int births = 0, deaths = 0;  // since previous sample
    int deathsStarvation = 0, deathsOldAge = 0, deathsPredation = 0;
    int birthsSuppressed = 0;
    double meanEnergyFrac = 0.0;
    double meanAge = 0.0;
    double meanAgeAtDeath = 0.0;     // over deaths since previous sample (NaN→0 if none)
    double medianAgeAtDeath = 0.0;
    double offspringPerDeath = 0.0;  // lifetime reproductive output of those that died
    double meanGeneration = 0.0;
    std::uint32_t maxGeneration = 0;
    double diversity = 0.0;          // mean pairwise genome distance (prey)
    double foodTotal = 0.0;
    double foodFraction = 0.0;       // food / current capacity
    double temperatureOffset = 0.0;
    int clusters = 0;
    int lineages = 0;                // distinct founders alive
    double stepMs = 0.0;             // wall clock per tick (not deterministic)
    std::array<double, kNumTraits> traitMean{};
    std::array<double, kNumTraits> traitSd{};
    std::array<double, kNumTraits> predTraitMean{};

    Json toJson() const;
};

class Statistics {
public:
    void configure(int maxSamples) { maxSamples_ = maxSamples; samples_.clear(); resetInterval(); }

    void onBirth() { ++births_; }
    void onSuppressedBirth() { ++suppressed_; }
    void onDeath(int cause, std::uint32_t age, std::uint32_t offspring);

    // Fills interval counters into `s`, appends it, resets interval counters.
    void push(Sample s);

    const std::vector<Sample>& samples() const { return samples_; }
    std::vector<Sample>& mutableSamples() { return samples_; }
    const Sample* latest() const { return samples_.empty() ? nullptr : &samples_.back(); }

    std::uint64_t totalBirths() const { return totalBirths_; }
    std::uint64_t totalDeaths() const { return totalDeaths_; }
    void setTotals(std::uint64_t b, std::uint64_t d) { totalBirths_ = b; totalDeaths_ = d; }

    static std::string csvHeader();
    static std::string csvRow(const Sample& s);

    friend class Checkpoint;

private:
    void resetInterval();

    int maxSamples_ = 20000;
    std::vector<Sample> samples_;
    int births_ = 0, deaths_ = 0, starv_ = 0, old_ = 0, pred_ = 0, suppressed_ = 0;
    std::vector<std::uint32_t> deathAges_;
    double offspringSum_ = 0.0;
    std::uint64_t totalBirths_ = 0, totalDeaths_ = 0;
};

}  // namespace evo
