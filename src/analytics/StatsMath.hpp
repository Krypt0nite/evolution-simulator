#pragma once
// Small, dependency-free statistics helpers for comparing experiment runs.
// The unit of replication is the independent simulation run (seed), never the
// individual organism.
#include <cmath>
#include <vector>

namespace evo::stats {

double mean(const std::vector<double>& v);
double sd(const std::vector<double>& v);  // sample standard deviation (n-1)
double median(std::vector<double> v);
double tCritical95(int df);               // two-sided 95% Student-t critical value

struct Interval { double lo = 0, hi = 0; };
Interval meanCI95(const std::vector<double>& v);
Interval wilson95(int successes, int n);  // CI for a proportion (e.g. extinction)

// Cohen's d with pooled SD (b relative to a). NaN if undefined.
double cohensD(const std::vector<double>& a, const std::vector<double>& b);
// Welch's t-test two-sided p-value approximation via the t distribution.
double welchP(const std::vector<double>& a, const std::vector<double>& b);

}  // namespace evo::stats
