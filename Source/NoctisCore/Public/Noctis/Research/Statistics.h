// Small statistics library used by the HypothesisSystem (deterministic, permutation-based where possible).
#pragma once

#include "Noctis/Core/Platform.h"

#include <string>
#include <vector>

namespace noctis
{
struct TestResult
{
    std::string method;
    int n = 0;
    int nA = 0;
    int nB = 0;
    double statistic = 0.0;
    double pValue = 1.0;
    double effectSize = 0.0;
    std::string effectName;
    double ciLow = 0.0;
    double ciHigh = 0.0;
    double medianA = 0.0;
    double medianB = 0.0;
};

NOCTIS_API double mean(const std::vector<double>& v);
NOCTIS_API double median(std::vector<double> v);
NOCTIS_API double stddev(const std::vector<double>& v);
// Two-sided Fisher exact test for a 2x2 table [[a, b], [c, d]] with odds ratio (Haldane) and Woolf 95 % CI.
NOCTIS_API TestResult fisherExact(int a, int b, int c, int d);
// Mann–Whitney U with permutation p-value; effect = rank-biserial correlation, bootstrap 95 % CI.
NOCTIS_API TestResult mannWhitney(const std::vector<double>& a, const std::vector<double>& b, u64 seed, int permutations = 3000);
// Spearman rank correlation with permutation p-value and bootstrap 95 % CI.
NOCTIS_API TestResult spearman(const std::vector<double>& x, const std::vector<double>& y, u64 seed, int permutations = 3000);
NOCTIS_API std::vector<double> ranks(const std::vector<double>& v);
} // namespace noctis
