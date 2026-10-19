#pragma once
// Plant biomass on a regular grid. This is where energy enters the modelled
// ecosystem: each cell grows logistically toward a capacity K(cell, t).
//
//   growth  = r·b·(1 - b/K) + s·K·(1 - b/K)      (b < K)
//   decay   = 0.05·(b - K)                        (b > K, e.g. after K drops)
//
// s (seedRate) is a small spontaneous inflow so emptied cells can recover.
// K(cell, t) = capacity × spatialProfile(cell) × season(t) × eventMultiplier.
#include <cstdint>
#include <vector>

#include "core/Random.hpp"
#include "core/SimulationConfig.hpp"
#include "core/Vec2.hpp"

namespace evo {

class ResourceField {
public:
    void configure(const SimulationConfig& cfg, Rng& rng);

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    double cellSize() const { return cell_; }
    std::size_t cellCount() const { return biomass_.size(); }

    int cellAt(Vec2 p) const {
        int cx = static_cast<int>(p.x / cell_), cy = static_cast<int>(p.y / cell_);
        cx = cx < 0 ? 0 : (cx >= cols_ ? cols_ - 1 : cx);
        cy = cy < 0 ? 0 : (cy >= rows_ ? rows_ - 1 : cy);
        return cy * cols_ + cx;
    }
    Vec2 cellCenter(int c) const {
        return {(c % cols_ + 0.5) * cell_, (c / cols_ + 0.5) * cell_};
    }
    double biomass(int c) const { return biomass_[static_cast<std::size_t>(c)]; }
    double capacityAt(int c) const { return profile_[static_cast<std::size_t>(c)] * kScale_; }
    double maxCapacity() const { return capacity_; }

    // Removes up to `amount` from cell c; returns the amount actually taken.
    double take(int c, double amount);
    // Sets biomass to zero inside a disc; returns energy removed.
    double depleteDisc(Vec2 center, double radius);

    // Advances one tick. Returns {energy added, energy decayed}.
    struct Flow { double added = 0.0, decayed = 0.0; };
    Flow regrow(std::uint64_t tick);

    void relocatePatches(Rng& rng);  // new random patch centres (clustered/shifting)
    void setMultiplier(double m) { multiplier_ = m; }
    double multiplier() const { return multiplier_; }
    void setRegrowthRate(double r) { r_ = r; }
    double regrowthRate() const { return r_; }

    double total() const;
    double totalCapacity() const;  // current Σ K
    const std::vector<double>& biomassData() const { return biomass_; }
    const std::vector<Vec2>& patches() const { return patches_; }

    // Serialization access.
    std::vector<double>& mutableBiomass() { return biomass_; }
    void restore(const std::vector<Vec2>& patches, double multiplier, double regrowthRate, std::uint64_t tick);

private:
    void rebuildProfile();
    void updateScale(std::uint64_t tick);

    std::string mode_;
    int cols_ = 1, rows_ = 1;
    double cell_ = 5.0;
    double capacity_ = 10.0;
    double r_ = 0.02, seed_ = 0.0005;
    double patchRadius_ = 50.0, background_ = 0.1;
    double seasonAmp_ = 0.0;
    int seasonPeriod_ = 2000;
    int shiftPeriod_ = 3000;
    double multiplier_ = 1.0;
    double kScale_ = 10.0;  // capacity × season × multiplier for the current tick
    std::vector<Vec2> patches_;
    std::vector<double> profile_;  // spatial profile in [0,1]
    std::vector<double> biomass_;
};

}  // namespace evo
