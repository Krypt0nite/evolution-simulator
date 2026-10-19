#pragma once
// Phenotype: capabilities and costs expressed from a genome under the current
// configuration. Kept separate from the genotype so the mapping (and its
// trade-offs) lives in one documented function.
#include "core/SimulationConfig.hpp"
#include "organisms/Genome.hpp"

namespace evo {

enum class Kind : std::uint8_t { Prey = 0, Predator = 1 };

struct Phenotype {
    double maxSpeed = 1.0;      // world units / tick
    double vision = 10.0;       // world units
    double size = 1.0;
    double maxEnergy = 60.0;
    double biteRate = 1.0;      // biomass per tick
    double assimilation = 0.5;  // fraction of eaten energy kept
    double basalCost = 0.05;    // energy / tick before thermal & hazard costs
    double moveCoeff = 0.01;    // movement cost = moveCoeff × distance²
    double thermalCoeff = 0.1;  // thermal cost = thermalCoeff × (T - optimum)²
    double maturity = 100.0;
    double lifespan = 1000.0;
    double fertility = 0.2;
    double investment = 0.4;
    double reproThreshold = 36.0;  // energy needed to reproduce
};

Phenotype express(const Genome& g, Kind kind, const SimulationConfig& cfg);

}  // namespace evo
