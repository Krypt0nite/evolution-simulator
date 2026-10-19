#include "core/DetMath.hpp"
#include "organisms/Phenotype.hpp"

#include <cmath>

namespace evo {

Phenotype express(const Genome& g, Kind kind, const SimulationConfig& cfg) {
    const auto& oc = cfg.organisms;
    Phenotype p;
    p.size = g[BodySize];
    p.maxSpeed = g[Speed];
    p.vision = g[Vision];
    if (kind == Kind::Predator && p.vision < cfg.predators.minVision) p.vision = cfg.predators.minVision;
    p.maxEnergy = oc.energyPerSize * p.size;
    if (kind == Kind::Predator) p.maxEnergy *= cfg.predators.storageMultiplier;
    p.biteRate = oc.biteRate * p.size * std::sqrt(g[Metabolism]);
    p.assimilation = g[Efficiency];

    // Kleiber-like maintenance scaling (size^0.75), multiplied by metabolic
    // rate, gut cost (0.5 + efficiency) and the cost of a long-lived soma.
    const double allometric = det::pow(p.size, 0.75);
    const double lifespanFactor = 1.0 + oc.lifespanCost * (g[Lifespan] / geneSpec(Lifespan).max);
    p.basalCost = oc.basalRate * allometric * g[Metabolism] * (0.5 + g[Efficiency]) * lifespanFactor
                + oc.visionCost * p.vision;
    if (kind == Kind::Predator) p.basalCost *= cfg.predators.basalMultiplier;

    // Cost of transport scales sublinearly with mass (∝ M^0.7), so per unit of
    // body mass small organisms pay more to move (Schmidt-Nielsen 1972).
    p.moveCoeff = oc.moveCost * det::pow(p.size, 0.7);
    p.thermalCoeff = oc.thermalCost * allometric;
    p.maturity = g[Maturity];
    p.lifespan = g[Lifespan];
    p.fertility = g[Fertility];
    p.investment = g[Investment];
    p.reproThreshold = cfg.reproduction.thresholdFrac * p.maxEnergy;
    return p;
}

}  // namespace evo
