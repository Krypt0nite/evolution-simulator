#include "analytics/Statistics.hpp"

#include <algorithm>
#include <sstream>

#include "organisms/Organism.hpp"

namespace evo {

Json EnergyLedger::toJson() const {
    Json j = Json::object();
    j["initial"] = Json(initial); j["inflow"] = Json(inflow); j["introduced"] = Json(introduced);
    j["decay"] = Json(decay); j["depleted"] = Json(depleted); j["digestion"] = Json(digestion);
    j["basal"] = Json(basal); j["movement"] = Json(movement); j["thermal"] = Json(thermal);
    j["hazard"] = Json(hazard); j["hunting"] = Json(hunting); j["reproduction"] = Json(reproduction);
    j["overflow"] = Json(overflow); j["carcass"] = Json(carcass);
    j["assimilatedPlants"] = Json(assimilatedPlants); j["assimilatedPrey"] = Json(assimilatedPrey);
    return j;
}

Json Sample::toJson() const {
    Json j = Json::object();
    j["tick"] = Json(tick); j["population"] = Json(population); j["prey"] = Json(prey);
    j["predators"] = Json(predators); j["births"] = Json(births); j["deaths"] = Json(deaths);
    j["deathsStarvation"] = Json(deathsStarvation); j["deathsOldAge"] = Json(deathsOldAge);
    j["deathsPredation"] = Json(deathsPredation); j["birthsSuppressed"] = Json(birthsSuppressed);
    j["meanEnergyFrac"] = Json(meanEnergyFrac); j["meanAge"] = Json(meanAge);
    j["meanAgeAtDeath"] = Json(meanAgeAtDeath); j["medianAgeAtDeath"] = Json(medianAgeAtDeath);
    j["offspringPerDeath"] = Json(offspringPerDeath);
    j["meanGeneration"] = Json(meanGeneration); j["maxGeneration"] = Json(static_cast<std::uint64_t>(maxGeneration));
    j["diversity"] = Json(diversity); j["foodTotal"] = Json(foodTotal); j["foodFraction"] = Json(foodFraction);
    j["temperatureOffset"] = Json(temperatureOffset);
    j["clusters"] = Json(clusters); j["lineages"] = Json(lineages); j["stepMs"] = Json(stepMs);
    Json tm = Json::object(), ts = Json::object(), pm = Json::object();
    for (int g = 0; g < kNumTraits; ++g) {
        tm[geneSpec(g).name] = Json(traitMean[static_cast<std::size_t>(g)]);
        ts[geneSpec(g).name] = Json(traitSd[static_cast<std::size_t>(g)]);
        pm[geneSpec(g).name] = Json(predTraitMean[static_cast<std::size_t>(g)]);
    }
    j["traitMean"] = tm; j["traitSd"] = ts; j["predTraitMean"] = pm;
    return j;
}

void Statistics::resetInterval() {
    births_ = deaths_ = starv_ = old_ = pred_ = suppressed_ = 0;
    deathAges_.clear();
    offspringSum_ = 0.0;
}

void Statistics::onDeath(int cause, std::uint32_t age, std::uint32_t offspring) {
    ++deaths_;
    ++totalDeaths_;
    if (cause == static_cast<int>(DeathCause::Starvation)) ++starv_;
    else if (cause == static_cast<int>(DeathCause::OldAge)) ++old_;
    else if (cause == static_cast<int>(DeathCause::Predation)) ++pred_;
    deathAges_.push_back(age);
    offspringSum_ += offspring;
}

void Statistics::push(Sample s) {
    totalBirths_ += static_cast<std::uint64_t>(births_);
    s.births = births_;
    s.deaths = deaths_;
    s.deathsStarvation = starv_;
    s.deathsOldAge = old_;
    s.deathsPredation = pred_;
    s.birthsSuppressed = suppressed_;
    if (!deathAges_.empty()) {
        double sum = 0;
        for (auto a : deathAges_) sum += a;
        s.meanAgeAtDeath = sum / deathAges_.size();
        std::sort(deathAges_.begin(), deathAges_.end());
        const std::size_t n = deathAges_.size();
        s.medianAgeAtDeath = n % 2 ? deathAges_[n / 2] : 0.5 * (deathAges_[n / 2 - 1] + deathAges_[n / 2]);
        s.offspringPerDeath = offspringSum_ / n;
    }
    samples_.push_back(s);
    if (static_cast<int>(samples_.size()) > maxSamples_) {
        // Thin older history 2:1, always keeping the first and the latest sample.
        std::vector<Sample> thinned;
        thinned.reserve(samples_.size() / 2 + 2);
        for (std::size_t i = 0; i + 1 < samples_.size(); i += 2) thinned.push_back(samples_[i]);
        thinned.push_back(samples_.back());
        samples_ = std::move(thinned);
    }
    resetInterval();
}

std::string Statistics::csvHeader() {
    std::ostringstream os;
    os << "tick,population,prey,predators,births,deaths,deaths_starvation,deaths_old_age,deaths_predation,"
          "births_suppressed,mean_energy_frac,mean_age,mean_age_at_death,median_age_at_death,offspring_per_death,"
          "mean_generation,max_generation,diversity,food_total,food_fraction,temperature_offset,clusters,lineages,step_ms";
    for (int g = 0; g < kNumTraits; ++g) os << ",mean_" << geneSpec(g).name;
    for (int g = 0; g < kNumTraits; ++g) os << ",sd_" << geneSpec(g).name;
    for (int g = 0; g < kNumTraits; ++g) os << ",pred_mean_" << geneSpec(g).name;
    return os.str();
}

std::string Statistics::csvRow(const Sample& s) {
    std::ostringstream os;
    os << s.tick << ',' << s.population << ',' << s.prey << ',' << s.predators << ',' << s.births << ','
       << s.deaths << ',' << s.deathsStarvation << ',' << s.deathsOldAge << ',' << s.deathsPredation << ','
       << s.birthsSuppressed << ',' << formatNumber(s.meanEnergyFrac) << ',' << formatNumber(s.meanAge) << ','
       << formatNumber(s.meanAgeAtDeath) << ',' << formatNumber(s.medianAgeAtDeath) << ','
       << formatNumber(s.offspringPerDeath) << ',' << formatNumber(s.meanGeneration) << ',' << s.maxGeneration << ','
       << formatNumber(s.diversity) << ',' << formatNumber(s.foodTotal) << ',' << formatNumber(s.foodFraction) << ','
       << formatNumber(s.temperatureOffset) << ',' << s.clusters << ',' << s.lineages << ',' << formatNumber(s.stepMs);
    for (double v : s.traitMean) os << ',' << formatNumber(v);
    for (double v : s.traitSd) os << ',' << formatNumber(v);
    for (double v : s.predTraitMean) os << ',' << formatNumber(v);
    return os.str();
}

}  // namespace evo
