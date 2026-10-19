#pragma once
// Uniform bucket grid for neighbourhood queries. Rebuilt from scratch with a
// counting sort (two passes, no per-cell allocations), so build is O(n) and the
// memory is reused tick after tick.
#include <cstdint>
#include <vector>

#include "core/Vec2.hpp"

namespace evo {

class SpatialGrid {
public:
    void configure(double width, double height, double cellSize);

    // positions[i] belongs to item i. Items with include[i]==false are skipped
    // (pass an empty vector to include all).
    void build(const std::vector<Vec2>& positions, const std::vector<std::uint8_t>& include = {});

    // Calls fn(index) for every item whose position is within `radius` of
    // `center` (inclusive). Exact: no candidate inside the radius is omitted.
    template <typename Fn>
    void forEachInRadius(Vec2 center, double radius, Fn&& fn) const {
        const double r2 = radius * radius;
        int x0 = cellX(center.x - radius), x1 = cellX(center.x + radius);
        int y0 = cellY(center.y - radius), y1 = cellY(center.y + radius);
        for (int cy = y0; cy <= y1; ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                const int c = cy * cols_ + cx;
                for (std::uint32_t k = start_[c]; k < start_[c + 1]; ++k) {
                    const std::uint32_t idx = items_[k];
                    if (distanceSq((*positions_)[idx], center) <= r2) fn(idx);
                }
            }
        }
    }

    std::vector<std::uint32_t> queryRadius(Vec2 center, double radius) const;

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    double cellSize() const { return cell_; }

private:
    int cellX(double x) const {
        int c = static_cast<int>(x * inv_);
        return c < 0 ? 0 : (c >= cols_ ? cols_ - 1 : c);
    }
    int cellY(double y) const {
        int c = static_cast<int>(y * inv_);
        return c < 0 ? 0 : (c >= rows_ ? rows_ - 1 : c);
    }

    double cell_ = 10.0, inv_ = 0.1;
    int cols_ = 1, rows_ = 1;
    std::vector<std::uint32_t> start_;   // size cols*rows+1, prefix sums
    std::vector<std::uint32_t> items_;   // item indices grouped by cell
    std::vector<std::uint32_t> cellOf_;  // scratch
    std::vector<std::uint32_t> cursor_;  // scratch
    const std::vector<Vec2>* positions_ = nullptr;
};

}  // namespace evo
