#pragma once
// Seeded random number generation.
//
// std::mt19937_64's output sequence is fixed by the C++ standard, but the
// standard *distributions* and std::shuffle are implementation-defined. To get
// identical sequences under libstdc++ (native) and libc++ (Emscripten), all
// distributions used by the simulation are implemented here.
#include "core/DetMath.hpp"
#include <cmath>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace evo {

class Rng {
public:
    explicit Rng(std::uint64_t seed = 1) : eng_(seed) {}

    void seed(std::uint64_t s) { eng_.seed(s); hasSpare_ = false; }

    std::uint64_t nextU64() { return eng_(); }

    // Uniform in [0, 1) with 53 bits of precision.
    double uniform() { return static_cast<double>(eng_() >> 11) * 0x1.0p-53; }

    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }

    bool chance(double p) { return p > 0.0 && uniform() < p; }

    // Uniform integer in [0, n) using rejection to avoid modulo bias.
    std::uint64_t below(std::uint64_t n) {
        if (n <= 1) return 0;
        const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % n);
        std::uint64_t x;
        do { x = eng_(); } while (x >= limit);
        return x % n;
    }

    // Standard normal via Box-Muller (polar form avoids trig).
    double normal() {
        if (hasSpare_) { hasSpare_ = false; return spare_; }
        double u, v, s;
        do {
            u = uniform() * 2.0 - 1.0;
            v = uniform() * 2.0 - 1.0;
            s = u * u + v * v;
        } while (s >= 1.0 || s == 0.0);
        const double m = std::sqrt(-2.0 * det::log(s) / s);
        spare_ = v * m;
        hasSpare_ = true;
        return u * m;
    }

    double normal(double mean, double sd) { return mean + sd * normal(); }

    template <typename T>
    void shuffle(std::vector<T>& v) {
        for (std::size_t i = v.size(); i > 1; --i) {
            std::size_t j = static_cast<std::size_t>(below(i));
            std::swap(v[i - 1], v[j]);
        }
    }

    // Full state as text (mt19937_64 stream format is standardised) + Box-Muller spare.
    std::string serialize() const {
        std::ostringstream os;
        os << eng_ << ' ' << (hasSpare_ ? 1 : 0) << ' ';
        os.precision(17);
        os << std::hexfloat << spare_;
        return os.str();
    }

    void deserialize(const std::string& s) {
        std::istringstream is(s);
        is >> eng_;
        int has = 0;
        is >> has;
        std::string sp;
        is >> sp;
        spare_ = std::strtod(sp.c_str(), nullptr);
        hasSpare_ = has != 0;
    }

    // Portable fingerprint inputs: the engine's text form is standardised;
    // the spare is exposed as a raw double (hexfloat text is not portable).
    std::string engineState() const {
        std::ostringstream os;
        os << eng_;
        return os.str();
    }
    double spare() const { return hasSpare_ ? spare_ : 0.0; }
    // Portable fingerprint of the full generator state: the next draws of a copy.
    std::uint64_t fingerprint() const {
        std::mt19937_64 copy = eng_;
        std::uint64_t h = 0;
        for (int i = 0; i < 4; ++i) h = (h ^ copy()) * 0x9E3779B97F4A7C15ULL;
        return h;
    }
    bool hasSpare() const { return hasSpare_; }

    bool operator==(const Rng& o) const { return eng_ == o.eng_ && hasSpare_ == o.hasSpare_; }

private:
    std::mt19937_64 eng_;
    double spare_ = 0.0;
    bool hasSpare_ = false;
};

}  // namespace evo
