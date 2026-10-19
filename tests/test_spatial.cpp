#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "core/Random.hpp"
#include "core/SpatialGrid.hpp"

using namespace evo;

TEST_CASE("Spatial grid queries match brute force exactly", "[spatial][property]") {
    Rng rng(11);
    for (int trial = 0; trial < 40; ++trial) {
        const double W = rng.uniform(50, 500), H = rng.uniform(50, 500);
        const double cell = rng.uniform(2, 40);
        const int n = static_cast<int>(rng.uniform(0, 600));
        std::vector<Vec2> pts(static_cast<std::size_t>(n));
        std::vector<std::uint8_t> include(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            // Include points exactly on borders and corners.
            const double x = rng.chance(0.05) ? 0.0 : rng.uniform(0, W);
            const double y = rng.chance(0.05) ? H - 1e-9 : rng.uniform(0, H);
            pts[i] = {x, y};
            include[i] = rng.chance(0.9);
        }
        SpatialGrid grid;
        grid.configure(W, H, cell);
        grid.build(pts, include);
        for (int q = 0; q < 50; ++q) {
            const Vec2 c{rng.uniform(-20, W + 20), rng.uniform(-20, H + 20)};
            const double r = rng.chance(0.1) ? rng.uniform(W, 2 * W) : rng.uniform(0, 60);
            auto got = grid.queryRadius(c, r);
            std::vector<std::uint32_t> want;
            for (int i = 0; i < n; ++i)
                if (include[i] && distanceSq(pts[i], c) <= r * r) want.push_back(static_cast<std::uint32_t>(i));
            std::sort(got.begin(), got.end());
            REQUIRE(got == want);
        }
    }
}

TEST_CASE("Spatial grid query order is deterministic and ascending within a cell", "[spatial]") {
    std::vector<Vec2> pts = {{5, 5}, {5.5, 5.5}, {6, 6}, {50, 50}};
    SpatialGrid grid;
    grid.configure(100, 100, 10);
    grid.build(pts);
    auto r = grid.queryRadius({5, 5}, 3);
    CHECK(r == std::vector<std::uint32_t>{0, 1, 2});
}
