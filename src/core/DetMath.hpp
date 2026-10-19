#pragma once
// Deterministic transcendental functions.
//
// libm implementations (glibc, musl, Apple, MSVC) may differ in the last bit
// for exp/log/pow/sin/tanh. In a chaotic simulation one differing bit
// eventually changes the whole trajectory, so every *state-affecting*
// computation uses these instead. They are built only from +, -, *, / and
// exact power-of-two scaling, which IEEE-754 requires to be correctly rounded,
// so results are bit-identical on every conforming platform — provided the
// compiler does not contract a*b+c into FMA (CMake sets -ffp-contract=off).
//
// Accuracy: within a few ulp of libm over the ranges the simulation uses
// (tested in tests/test_detmath.cpp). sqrt is correctly rounded by IEEE-754
// and needs no replacement.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace evo::det {

inline constexpr double kLn2Hi = 6.93147180369123816490e-01;  // high bits of ln 2
inline constexpr double kLn2Lo = 1.90821492927058770002e-10;  // ln 2 - kLn2Hi
inline constexpr double kInvLn2 = 1.44269504088896338700e+00;
inline constexpr double kPi = 3.14159265358979311600e+00;
inline constexpr double kTwoPi = 6.28318530717958623200e+00;

// Exact scaling by 2^k through the exponent bits (no libm involved).
inline double scale2(double x, int k) {
    // Split large |k| so each factor is a normal power of two.
    while (k > 1000) { x *= 0x1p1000; k -= 1000; }
    while (k < -1000) { x *= 0x1p-1000; k += 1000; }
    const std::uint64_t bits = static_cast<std::uint64_t>(k + 1023) << 52;
    double f;
    std::memcpy(&f, &bits, sizeof f);
    return x * f;
}

inline double exp(double x) {
    if (x != x) return x;
    if (x > 709.78) return std::numeric_limits<double>::infinity();
    if (x < -745.0) return 0.0;
    // x = k·ln2 + r, |r| ≤ ln2/2. Round-to-nearest k without libm.
    const double kd = x * kInvLn2;
    const int k = static_cast<int>(kd < 0 ? kd - 0.5 : kd + 0.5);
    const double r = (x - k * kLn2Hi) - k * kLn2Lo;
    // Taylor series of e^r to degree 13 (error < 1e-17 for |r| ≤ 0.35).
    double p = 1.0 / 6227020800.0;  // 1/13!
    p = p * r + 1.0 / 479001600.0;
    p = p * r + 1.0 / 39916800.0;
    p = p * r + 1.0 / 3628800.0;
    p = p * r + 1.0 / 362880.0;
    p = p * r + 1.0 / 40320.0;
    p = p * r + 1.0 / 5040.0;
    p = p * r + 1.0 / 720.0;
    p = p * r + 1.0 / 120.0;
    p = p * r + 1.0 / 24.0;
    p = p * r + 1.0 / 6.0;
    p = p * r + 0.5;
    p = p * r + 1.0;
    p = p * r + 1.0;
    return scale2(p, k);
}

inline double log(double x) {
    if (x != x || x < 0.0) return std::numeric_limits<double>::quiet_NaN();
    if (x == 0.0) return -std::numeric_limits<double>::infinity();
    if (x == std::numeric_limits<double>::infinity()) return x;
    // Decompose x = m · 2^e with m in [sqrt(0.5), sqrt(2)) using the bits.
    std::uint64_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    int e = static_cast<int>((bits >> 52) & 0x7ff);
    if (e == 0) {  // subnormal: normalise first
        x *= 0x1p54;
        std::memcpy(&bits, &x, sizeof bits);
        e = static_cast<int>((bits >> 52) & 0x7ff) - 54;
    }
    e -= 1023;
    bits = (bits & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL;
    double m;
    std::memcpy(&m, &bits, sizeof m);
    if (m > 1.41421356237309504880) { m *= 0.5; ++e; }
    // log(m) = 2·atanh(s), s = (m-1)/(m+1), |s| ≤ 0.1716.
    const double s = (m - 1.0) / (m + 1.0);
    const double s2 = s * s;
    double t = 1.0 / 25.0;
    t = t * s2 + 1.0 / 23.0;
    t = t * s2 + 1.0 / 21.0;
    t = t * s2 + 1.0 / 19.0;
    t = t * s2 + 1.0 / 17.0;
    t = t * s2 + 1.0 / 15.0;
    t = t * s2 + 1.0 / 13.0;
    t = t * s2 + 1.0 / 11.0;
    t = t * s2 + 1.0 / 9.0;
    t = t * s2 + 1.0 / 7.0;
    t = t * s2 + 1.0 / 5.0;
    t = t * s2 + 1.0 / 3.0;
    t = t * s2 + 1.0;
    const double logm = 2.0 * s * t;
    return (e * kLn2Hi + logm) + e * kLn2Lo;
}

// x^y for x > 0 (all uses in the simulation have positive bases).
inline double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 1.0) return 1.0;
    if (x <= 0.0) return std::pow(x, y);  // not used by the engine; keep libm semantics
    return det::exp(y * det::log(x));
}

inline double sin(double x) {
    if (x != x || x == std::numeric_limits<double>::infinity() || x == -std::numeric_limits<double>::infinity())
        return std::numeric_limits<double>::quiet_NaN();
    // Reduce to [-π, π] (engine arguments are already within [0, 2π]).
    if (x > kPi || x < -kPi) {
        const double n = x / kTwoPi;
        const double k = static_cast<double>(static_cast<long long>(n < 0 ? n - 0.5 : n + 0.5));
        x -= k * kTwoPi;
    }
    // Use symmetry to reduce to [-π/2, π/2].
    if (x > kPi / 2) x = kPi - x;
    else if (x < -kPi / 2) x = -kPi - x;
    const double x2 = x * x;
    // Taylor series to x^23 (error < 1e-17 on [-π/2, π/2]).
    double p = -1.0 / 25852016738884976640000.0;  // -1/23!
    p = p * x2 + 1.0 / 51090942171709440000.0;    // 1/21!
    p = p * x2 - 1.0 / 121645100408832000.0;      // -1/19!
    p = p * x2 + 1.0 / 355687428096000.0;         // 1/17!
    p = p * x2 - 1.0 / 1307674368000.0;           // -1/15!
    p = p * x2 + 1.0 / 6227020800.0;              // 1/13!
    p = p * x2 - 1.0 / 39916800.0;                // -1/11!
    p = p * x2 + 1.0 / 362880.0;                  // 1/9!
    p = p * x2 - 1.0 / 5040.0;                    // -1/7!
    p = p * x2 + 1.0 / 120.0;                     // 1/5!
    p = p * x2 - 1.0 / 6.0;                       // -1/3!
    p = p * x2 + 1.0;
    return x * p;
}

inline double tanh(double x) {
    if (x != x) return x;
    if (x > 20.0) return 1.0;
    if (x < -20.0) return -1.0;
    const double ax = x < 0 ? -x : x;
    double t;
    if (ax < 0.05) {
        // Series avoids cancellation near 0 (next term < 1e-20 relative).
        const double x2 = ax * ax;
        double p = 21844.0 / 6081075.0;
        p = p * x2 - 1382.0 / 155925.0;
        p = p * x2 + 62.0 / 2835.0;
        p = p * x2 - 17.0 / 315.0;
        p = p * x2 + 2.0 / 15.0;
        p = p * x2 - 1.0 / 3.0;
        p = p * x2 + 1.0;
        t = ax * p;
    } else {
        const double e = det::exp(-2.0 * ax);
        t = (1.0 - e) / (1.0 + e);
    }
    return x < 0 ? -t : t;
}

}  // namespace evo::det
