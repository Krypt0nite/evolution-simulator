#include "core/DetMath.hpp"
#include "ecology/Environment.hpp"

#include <cmath>
#include <numbers>

namespace evo {

void Environment::configure(const SimulationConfig& cfg) {
    width_ = cfg.world.width;
    height_ = cfg.world.height;
    gradient_ = cfg.environment.temperatureGradient;
    offset_ = cfg.environment.temperatureOffset;
    cycleAmp_ = cfg.environment.cycleAmplitude;
    cyclePeriod_ = cfg.environment.cyclePeriod;
    hazards_ = cfg.environment.hazards;
    update(0);
}

void Environment::update(std::uint64_t tick) {
    if (cycleAmp_ == 0.0) { cycle_ = 0.0; return; }
    const double phase = 2.0 * std::numbers::pi *
                         static_cast<double>(tick % static_cast<std::uint64_t>(cyclePeriod_)) / cyclePeriod_;
    cycle_ = cycleAmp_ * det::sin(phase);
}

double Environment::hazardDrain(Vec2 p) const {
    double d = 0.0;
    for (const auto& h : hazards_)
        if (distanceSq(p, h.center) <= h.radius * h.radius) d += h.damage;
    return d;
}

}  // namespace evo
