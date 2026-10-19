#include "core/DetMath.hpp"
#include "ecology/ResourceField.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evo {

void ResourceField::configure(const SimulationConfig& cfg, Rng& rng) {
    const auto& rc = cfg.resources;
    mode_ = rc.distribution;
    cell_ = rc.cellSize;
    cols_ = std::max(1, static_cast<int>(std::ceil(cfg.world.width / cell_)));
    rows_ = std::max(1, static_cast<int>(std::ceil(cfg.world.height / cell_)));
    capacity_ = rc.capacity;
    r_ = rc.regrowthRate;
    seed_ = rc.seedRate;
    patchRadius_ = rc.patchRadius;
    background_ = rc.backgroundFraction;
    seasonAmp_ = rc.seasonAmplitude;
    seasonPeriod_ = rc.seasonPeriod;
    shiftPeriod_ = rc.shiftPeriod;
    multiplier_ = 1.0;
    patches_.clear();
    const auto n = static_cast<std::size_t>(cols_) * rows_;
    profile_.assign(n, 1.0);
    if (mode_ != "uniform") {
        for (int i = 0; i < rc.patchCount; ++i)
            patches_.push_back({rng.uniform(0.0, cfg.world.width), rng.uniform(0.0, cfg.world.height)});
    }
    rebuildProfile();
    updateScale(0);
    biomass_.assign(n, 0.0);
    for (std::size_t c = 0; c < n; ++c) biomass_[c] = rc.initialFill * profile_[c] * kScale_;
}

void ResourceField::rebuildProfile() {
    if (mode_ == "uniform") {
        std::fill(profile_.begin(), profile_.end(), 1.0);
        return;
    }
    const double sigma = std::max(1.0, patchRadius_ * 0.5);
    const double inv2s2 = 1.0 / (2.0 * sigma * sigma);
    const double cutoff2 = (3.0 * sigma) * (3.0 * sigma);
    for (std::size_t c = 0; c < profile_.size(); ++c) {
        const Vec2 p = cellCenter(static_cast<int>(c));
        double best = 0.0;
        for (const auto& q : patches_) {
            const double d2 = distanceSq(p, q);
            if (d2 > cutoff2) continue;
            best = std::max(best, det::exp(-d2 * inv2s2));
        }
        profile_[c] = background_ + (1.0 - background_) * best;
    }
}

void ResourceField::updateScale(std::uint64_t tick) {
    double season = 1.0;
    if (seasonAmp_ > 0.0) {
        const double phase = 2.0 * std::numbers::pi * static_cast<double>(tick % static_cast<std::uint64_t>(seasonPeriod_)) / seasonPeriod_;
        season = 1.0 + seasonAmp_ * det::sin(phase);
    }
    kScale_ = capacity_ * season * multiplier_;
}

void ResourceField::relocatePatches(Rng& rng) {
    if (patches_.empty()) return;
    const double w = cols_ * cell_, h = rows_ * cell_;
    for (auto& p : patches_) p = {rng.uniform(0.0, w), rng.uniform(0.0, h)};
    rebuildProfile();
}

double ResourceField::take(int c, double amount) {
    if (amount <= 0.0) return 0.0;
    double& b = biomass_[static_cast<std::size_t>(c)];
    const double t = std::min(b, amount);
    b -= t;
    return t;
}

double ResourceField::depleteDisc(Vec2 center, double radius) {
    double removed = 0.0;
    const double r2 = radius * radius;
    for (std::size_t c = 0; c < biomass_.size(); ++c) {
        if (distanceSq(cellCenter(static_cast<int>(c)), center) <= r2) {
            removed += biomass_[c];
            biomass_[c] = 0.0;
        }
    }
    return removed;
}

ResourceField::Flow ResourceField::regrow(std::uint64_t tick) {
    updateScale(tick);
    Flow f;
    for (std::size_t c = 0; c < biomass_.size(); ++c) {
        const double K = profile_[c] * kScale_;
        double& b = biomass_[c];
        if (K <= 0.0) {
            if (b > 0) { f.decayed += b; b = 0.0; }
            continue;
        }
        if (b < K) {
            const double room = 1.0 - b / K;
            double g = r_ * b * room + seed_ * K * room;
            if (b + g > K) g = K - b;
            b += g;
            f.added += g;
        } else if (b > K) {
            const double d = 0.05 * (b - K);
            b -= d;
            f.decayed += d;
        }
    }
    return f;
}

double ResourceField::total() const {
    double s = 0.0;
    for (double b : biomass_) s += b;
    return s;
}

double ResourceField::totalCapacity() const {
    double s = 0.0;
    for (double p : profile_) s += p;
    return s * kScale_;
}

void ResourceField::restore(const std::vector<Vec2>& patches, double multiplier, double regrowthRate, std::uint64_t tick) {
    patches_ = patches;
    multiplier_ = multiplier;
    r_ = regrowthRate;
    rebuildProfile();
    updateScale(tick);
}

}  // namespace evo
