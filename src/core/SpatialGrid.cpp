#include "core/SpatialGrid.hpp"

#include <algorithm>
#include <cmath>

namespace evo {

void SpatialGrid::configure(double width, double height, double cellSize) {
    cell_ = std::max(1.0, cellSize);
    inv_ = 1.0 / cell_;
    cols_ = std::max(1, static_cast<int>(std::ceil(width / cell_)));
    rows_ = std::max(1, static_cast<int>(std::ceil(height / cell_)));
    start_.assign(static_cast<std::size_t>(cols_) * rows_ + 1, 0);
}

void SpatialGrid::build(const std::vector<Vec2>& positions, const std::vector<std::uint8_t>& include) {
    positions_ = &positions;
    const std::size_t n = positions.size();
    const std::size_t cells = static_cast<std::size_t>(cols_) * rows_;
    std::fill(start_.begin(), start_.end(), 0u);
    cellOf_.resize(n);
    std::size_t count = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!include.empty() && !include[i]) { cellOf_[i] = UINT32_MAX; continue; }
        const auto c = static_cast<std::uint32_t>(cellY(positions[i].y) * cols_ + cellX(positions[i].x));
        cellOf_[i] = c;
        ++start_[c + 1];
        ++count;
    }
    for (std::size_t c = 0; c < cells; ++c) start_[c + 1] += start_[c];
    items_.resize(count);
    // Fill using a running cursor per cell; iterating i in order keeps items
    // within a cell in ascending index order (deterministic query order).
    std::vector<std::uint32_t>& cursor = cursor_;
    cursor.assign(start_.begin(), start_.end() - 1);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t c = cellOf_[i];
        if (c == UINT32_MAX) continue;
        items_[cursor[c]++] = static_cast<std::uint32_t>(i);
    }
}

std::vector<std::uint32_t> SpatialGrid::queryRadius(Vec2 center, double radius) const {
    std::vector<std::uint32_t> out;
    forEachInRadius(center, radius, [&](std::uint32_t i) { out.push_back(i); });
    return out;
}

}  // namespace evo
