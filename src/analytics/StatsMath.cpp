#include "analytics/StatsMath.hpp"

#include <algorithm>
#include <limits>

namespace evo::stats {

double mean(const std::vector<double>& v) {
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    double s = 0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}

double sd(const std::vector<double>& v) {
    if (v.size() < 2) return std::numeric_limits<double>::quiet_NaN();
    const double m = mean(v);
    double s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size() - 1));
}

double median(std::vector<double> v) {
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double tCritical95(int df) {
    static const double table[] = {0, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
                                   2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
                                   2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042};
    if (df <= 0) return std::numeric_limits<double>::quiet_NaN();
    if (df <= 30) return table[df];
    if (df <= 40) return 2.042 - (df - 30) * (2.042 - 2.021) / 10.0;
    if (df <= 60) return 2.021 - (df - 40) * (2.021 - 2.000) / 20.0;
    if (df <= 120) return 2.000 - (df - 60) * (2.000 - 1.980) / 60.0;
    return 1.960;
}

Interval meanCI95(const std::vector<double>& v) {
    const double m = mean(v);
    if (v.size() < 2) return {m, m};
    const double h = tCritical95(static_cast<int>(v.size()) - 1) * sd(v) / std::sqrt(static_cast<double>(v.size()));
    return {m - h, m + h};
}

Interval wilson95(int k, int n) {
    if (n <= 0) return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    const double z = 1.959964, p = static_cast<double>(k) / n;
    const double denom = 1 + z * z / n;
    const double centre = (p + z * z / (2.0 * n)) / denom;
    const double half = z * std::sqrt(p * (1 - p) / n + z * z / (4.0 * n * n)) / denom;
    return {std::max(0.0, centre - half), std::min(1.0, centre + half)};
}

double cohensD(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() < 2 || b.size() < 2) return std::numeric_limits<double>::quiet_NaN();
    const double sa = sd(a), sb = sd(b);
    const double na = static_cast<double>(a.size()), nb = static_cast<double>(b.size());
    const double pooled = std::sqrt(((na - 1) * sa * sa + (nb - 1) * sb * sb) / (na + nb - 2));
    if (pooled <= 1e-300) return mean(b) == mean(a) ? 0.0 : std::numeric_limits<double>::infinity();
    return (mean(b) - mean(a)) / pooled;
}

namespace {
// Regularised incomplete beta via continued fraction (Numerical Recipes style).
double betacf(double a, double b, double x) {
    const int maxIt = 200;
    const double eps = 3e-14, fpmin = 1e-300;
    double qab = a + b, qap = a + 1, qam = a - 1, c = 1, d = 1 - qab * x / qap;
    if (std::fabs(d) < fpmin) d = fpmin;
    d = 1 / d;
    double h = d;
    for (int m = 1; m <= maxIt; ++m) {
        const int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1 + aa * d; if (std::fabs(d) < fpmin) d = fpmin;
        c = 1 + aa / c; if (std::fabs(c) < fpmin) c = fpmin;
        d = 1 / d; h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1 + aa * d; if (std::fabs(d) < fpmin) d = fpmin;
        c = 1 + aa / c; if (std::fabs(c) < fpmin) c = fpmin;
        d = 1 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1) < eps) break;
    }
    return h;
}
double betai(double a, double b, double x) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    const double bt = std::exp(std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) + a * std::log(x) + b * std::log(1 - x));
    return x < (a + 1) / (a + b + 2) ? bt * betacf(a, b, x) / a : 1 - bt * betacf(b, a, 1 - x) / b;
}
}  // namespace

double welchP(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() < 2 || b.size() < 2) return std::numeric_limits<double>::quiet_NaN();
    const double va = sd(a) * sd(a) / a.size(), vb = sd(b) * sd(b) / b.size();
    if (va + vb <= 1e-300) return mean(a) == mean(b) ? 1.0 : 0.0;
    const double t = (mean(b) - mean(a)) / std::sqrt(va + vb);
    const double df = (va + vb) * (va + vb) /
                      (va * va / (a.size() - 1.0) + vb * vb / (b.size() - 1.0));
    return betai(0.5 * df, 0.5, df / (df + t * t));
}

}  // namespace evo::stats
