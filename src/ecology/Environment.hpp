#pragma once
// Abiotic environment: a temperature field and hazardous regions.
//   T(y, t) = offset + gradient·(2y/H - 1) + A·sin(2πt/P)
// The y-gradient creates distinct thermal niches (top cold, bottom warm), so
// sub-populations can diverge in thermalOptimum.
#include <cstdint>
#include <vector>

#include "core/SimulationConfig.hpp"
#include "core/Vec2.hpp"

namespace evo {

class Environment {
public:
    void configure(const SimulationConfig& cfg);
    void update(std::uint64_t tick);  // refresh time-varying global terms

    double temperature(Vec2 p) const {
        return offset_ + cycle_ + gradient_ * (2.0 * p.y / height_ - 1.0);
    }
    double hazardDrain(Vec2 p) const;

    double offset() const { return offset_; }
    void setOffset(double o) { offset_ = o; }
    double gradient() const { return gradient_; }
    double cycleValue() const { return cycle_; }

    std::vector<Hazard>& hazards() { return hazards_; }
    const std::vector<Hazard>& hazards() const { return hazards_; }

private:
    double width_ = 600, height_ = 400;
    double gradient_ = 0.6, offset_ = 0.0;
    double cycleAmp_ = 0.0;
    int cyclePeriod_ = 4000;
    double cycle_ = 0.0;
    std::vector<Hazard> hazards_;
};

}  // namespace evo
