#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "core/DetMath.hpp"
#include "core/Random.hpp"

using namespace evo;

namespace {
double relErr(double got, double want) {
    if (want == 0.0) return std::fabs(got);
    return std::fabs(got - want) / std::fabs(want);
}
}  // namespace

TEST_CASE("Deterministic exp/log/pow/sin/tanh match libm closely", "[detmath]") {
    Rng r(77);
    double worstExp = 0, worstLog = 0, worstPow = 0, worstSin = 0, worstTanh = 0;
    for (int i = 0; i < 200000; ++i) {
        const double x = r.uniform(-40.0, 40.0);
        worstExp = std::max(worstExp, relErr(det::exp(x), std::exp(x)));
        const double y = std::exp(r.uniform(-690.0, 690.0));
        worstLog = std::max(worstLog, std::fabs(det::log(y) - std::log(y)) / std::max(1.0, std::fabs(std::log(y))));
        const double base = r.uniform(0.3, 4.0), ex = r.uniform(0.0, 2.5);
        worstPow = std::max(worstPow, relErr(det::pow(base, ex), std::pow(base, ex)));
        const double a = r.uniform(0.0, 6.2831853);
        worstSin = std::max(worstSin, std::fabs(det::sin(a) - std::sin(a)));
        const double t = r.uniform(-25.0, 25.0) * (r.chance(0.3) ? 0.01 : 1.0);
        worstTanh = std::max(worstTanh, std::fabs(det::tanh(t) - std::tanh(t)));
    }
    INFO("worst exp " << worstExp << " log " << worstLog << " pow " << worstPow << " sin " << worstSin << " tanh " << worstTanh);
    CHECK(worstExp < 1e-15);
    CHECK(worstLog < 1e-15);
    CHECK(worstPow < 1e-14);
    CHECK(worstSin < 1e-15);
    CHECK(worstTanh < 1e-15);
    // Special values.
    CHECK(det::exp(0.0) == 1.0);
    CHECK(det::log(1.0) == 0.0);
    CHECK(det::pow(2.0, 0.0) == 1.0);
    CHECK(det::exp(-1000.0) == 0.0);
    CHECK(std::isinf(det::exp(1000.0)));
    CHECK(std::isnan(det::log(-1.0)));
    CHECK(det::tanh(30.0) == 1.0);
    CHECK(det::log(5e-324) < -744.0);  // subnormal input
}
