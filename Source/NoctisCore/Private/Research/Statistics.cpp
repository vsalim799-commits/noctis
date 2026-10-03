#include "Noctis/Research/Statistics.h"

#include "Noctis/Core/Random.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace noctis
{
double mean(const std::vector<double>& v)
{
    if (v.empty())
    {
        return 0.0;
    }
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

double median(std::vector<double> v)
{
    if (v.empty())
    {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double stddev(const std::vector<double>& v)
{
    if (v.size() < 2)
    {
        return 0.0;
    }
    const double m = mean(v);
    double s = 0.0;
    for (const double x : v)
    {
        s += (x - m) * (x - m);
    }
    return std::sqrt(s / static_cast<double>(v.size() - 1));
}

std::vector<double> ranks(const std::vector<double>& v)
{
    std::vector<size_t> idx(v.size());
    std::iota(idx.begin(), idx.end(), 0u);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return v[a] < v[b]; });
    std::vector<double> r(v.size());
    size_t i = 0;
    while (i < idx.size())
    {
        size_t j = i;
        while (j + 1 < idx.size() && v[idx[j + 1]] == v[idx[i]])
        {
            ++j;
        }
        const double avg = 0.5 * static_cast<double>(i + j) + 1.0;
        for (size_t k = i; k <= j; ++k)
        {
            r[idx[k]] = avg;
        }
        i = j + 1;
    }
    return r;
}

namespace statsimpl
{
double logChoose(int n, int k) { return std::lgamma(n + 1.0) - std::lgamma(k + 1.0) - std::lgamma(n - k + 1.0); }

double hypergeomP(int a, int rowA, int colA, int n)
{
    return std::exp(logChoose(colA, a) + logChoose(n - colA, rowA - a) - logChoose(n, rowA));
}

double rankBiserial(const std::vector<double>& a, const std::vector<double>& b)
{
    std::vector<double> all(a);
    all.insert(all.end(), b.begin(), b.end());
    const std::vector<double> r = ranks(all);
    double ra = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        ra += r[i];
    }
    const double na = static_cast<double>(a.size());
    const double nb = static_cast<double>(b.size());
    const double u = ra - na * (na + 1.0) / 2.0;
    return 2.0 * u / (na * nb) - 1.0; // >0 : a tends to be larger
}

double spearmanRho(const std::vector<double>& x, const std::vector<double>& y)
{
    const std::vector<double> rx = ranks(x);
    const std::vector<double> ry = ranks(y);
    const double mx = mean(rx);
    const double my = mean(ry);
    double sxy = 0.0;
    double sxx = 0.0;
    double syy = 0.0;
    for (size_t i = 0; i < rx.size(); ++i)
    {
        sxy += (rx[i] - mx) * (ry[i] - my);
        sxx += (rx[i] - mx) * (rx[i] - mx);
        syy += (ry[i] - my) * (ry[i] - my);
    }
    return (sxx > 0.0 && syy > 0.0) ? sxy / std::sqrt(sxx * syy) : 0.0;
}
} // namespace statsimpl

TestResult fisherExact(int a, int b, int c, int d)
{
    TestResult r;
    r.method = "Test exact de Fisher (bilatéral)";
    r.n = a + b + c + d;
    r.nA = a + b;
    r.nB = c + d;
    const int rowA = a + b;
    const int colA = a + c;
    const double pObs = statsimpl::hypergeomP(a, rowA, colA, r.n);
    double p = 0.0;
    const int lo = std::max(0, rowA + colA - r.n);
    const int hi = std::min(rowA, colA);
    for (int x = lo; x <= hi; ++x)
    {
        const double px = statsimpl::hypergeomP(x, rowA, colA, r.n);
        if (px <= pObs * (1.0 + 1e-7))
        {
            p += px;
        }
    }
    r.pValue = std::min(1.0, p);
    const double ha = a + 0.5;
    const double hb = b + 0.5;
    const double hc = c + 0.5;
    const double hd = d + 0.5;
    r.effectSize = (ha * hd) / (hb * hc);
    r.effectName = "rapport de cotes";
    const double se = std::sqrt(1.0 / ha + 1.0 / hb + 1.0 / hc + 1.0 / hd);
    r.ciLow = std::exp(std::log(r.effectSize) - 1.96 * se);
    r.ciHigh = std::exp(std::log(r.effectSize) + 1.96 * se);
    r.statistic = r.effectSize;
    return r;
}

TestResult mannWhitney(const std::vector<double>& a, const std::vector<double>& b, u64 seed, int permutations)
{
    TestResult r;
    r.method = "Mann–Whitney U (p par permutation)";
    r.nA = static_cast<int>(a.size());
    r.nB = static_cast<int>(b.size());
    r.n = r.nA + r.nB;
    r.medianA = median(a);
    r.medianB = median(b);
    if (a.empty() || b.empty())
    {
        return r;
    }
    const double obs = statsimpl::rankBiserial(a, b);
    r.effectSize = obs;
    r.effectName = "corrélation bisériale de rang";
    r.statistic = (obs + 1.0) / 2.0 * static_cast<double>(a.size()) * static_cast<double>(b.size());
    std::vector<double> all(a);
    all.insert(all.end(), b.begin(), b.end());
    Rng rng(seed, 0x5747);
    int extreme = 0;
    std::vector<double> pa(a.size());
    std::vector<double> pb(b.size());
    for (int p = 0; p < permutations; ++p)
    {
        for (size_t i = all.size() - 1; i > 0; --i)
        {
            const size_t j = static_cast<size_t>(rng.nextU32() % static_cast<u32>(i + 1));
            std::swap(all[i], all[j]);
        }
        std::copy(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(a.size()), pa.begin());
        std::copy(all.begin() + static_cast<std::ptrdiff_t>(a.size()), all.end(), pb.begin());
        if (std::fabs(statsimpl::rankBiserial(pa, pb)) >= std::fabs(obs) - 1e-12)
        {
            ++extreme;
        }
    }
    r.pValue = (static_cast<double>(extreme) + 1.0) / (static_cast<double>(permutations) + 1.0);
    // Bootstrap CI of the effect.
    std::vector<double> boots;
    for (int k = 0; k < 600; ++k)
    {
        for (size_t i = 0; i < a.size(); ++i)
        {
            pa[i] = a[static_cast<size_t>(rng.nextU32() % static_cast<u32>(a.size()))];
        }
        for (size_t i = 0; i < b.size(); ++i)
        {
            pb[i] = b[static_cast<size_t>(rng.nextU32() % static_cast<u32>(b.size()))];
        }
        boots.push_back(statsimpl::rankBiserial(pa, pb));
    }
    std::sort(boots.begin(), boots.end());
    r.ciLow = boots[static_cast<size_t>(0.025 * static_cast<double>(boots.size()))];
    r.ciHigh = boots[static_cast<size_t>(0.975 * static_cast<double>(boots.size() - 1))];
    return r;
}

TestResult spearman(const std::vector<double>& x, const std::vector<double>& y, u64 seed, int permutations)
{
    TestResult r;
    r.method = "Corrélation de Spearman (p par permutation)";
    r.n = static_cast<int>(std::min(x.size(), y.size()));
    if (r.n < 3)
    {
        return r;
    }
    std::vector<double> xs(x.begin(), x.begin() + r.n);
    std::vector<double> ys(y.begin(), y.begin() + r.n);
    const double rho = statsimpl::spearmanRho(xs, ys);
    r.statistic = rho;
    r.effectSize = rho;
    r.effectName = "rho de Spearman";
    Rng rng(seed, 0x59EA);
    std::vector<double> yp = ys;
    int extreme = 0;
    for (int p = 0; p < permutations; ++p)
    {
        for (size_t i = yp.size() - 1; i > 0; --i)
        {
            const size_t j = static_cast<size_t>(rng.nextU32() % static_cast<u32>(i + 1));
            std::swap(yp[i], yp[j]);
        }
        if (std::fabs(statsimpl::spearmanRho(xs, yp)) >= std::fabs(rho) - 1e-12)
        {
            ++extreme;
        }
    }
    r.pValue = (static_cast<double>(extreme) + 1.0) / (static_cast<double>(permutations) + 1.0);
    std::vector<double> boots;
    std::vector<double> bx(xs.size());
    std::vector<double> by(ys.size());
    for (int k = 0; k < 600; ++k)
    {
        for (size_t i = 0; i < xs.size(); ++i)
        {
            const size_t j = static_cast<size_t>(rng.nextU32() % static_cast<u32>(xs.size()));
            bx[i] = xs[j];
            by[i] = ys[j];
        }
        boots.push_back(statsimpl::spearmanRho(bx, by));
    }
    std::sort(boots.begin(), boots.end());
    r.ciLow = boots[static_cast<size_t>(0.025 * static_cast<double>(boots.size()))];
    r.ciHigh = boots[static_cast<size_t>(0.975 * static_cast<double>(boots.size() - 1))];
    return r;
}
} // namespace noctis
