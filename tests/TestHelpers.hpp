#pragma once
#include "core/Simulation.hpp"

namespace evo::test {

// Small, fast world used by most tests.
inline SimulationConfig smallConfig(std::uint64_t seed = 7) {
    SimulationConfig c;
    c.seed = seed;
    c.world.width = 200;
    c.world.height = 150;
    c.population.initialPrey = 120;
    c.resources.patchCount = 3;
    c.resources.patchRadius = 40;
    c.analytics.sampleInterval = 25;
    c.analytics.clusterInterval = 50;
    return c;
}

inline double totalEnergy(const Simulation& s) { return s.food().total() + s.organismEnergy(); }

inline double ledgerExpected(const Simulation& s) {
    const auto& L = s.ledger();
    return L.initial + L.inflow + L.introduced - L.totalLosses();
}

}  // namespace evo::test
