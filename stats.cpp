#include "stats.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <random>
#include <numeric>
#include <chrono>

namespace {
std::mt19937_64& Rng() {
    static std::random_device rd;
    static std::mt19937_64 rng(rd());
    return rng;
}

double Quantile(std::vector<double> values, double q) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double idx = q * (values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(idx));
    const size_t hi = static_cast<size_t>(std::ceil(idx));
    if (lo == hi) return values[lo];
    const double t = idx - lo;
    return values[lo] * (1.0 - t) + values[hi] * t;
}

std::unordered_map<int, std::pair<double, double>>& RayleighTable() {
    static std::unordered_map<int, std::pair<double, double>> table;
    static bool loaded = false;
    if (loaded) return table;
    loaded = true;

    std::ifstream file("rayleigh.csv");
    if (!file.is_open()) return table;

    std::string line;
    std::getline(file, line); // header
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string tok;
        int shots = 0;
        double p5 = 0.0, p95 = 0.0;
        int col = 0;
        while (std::getline(ss, tok, ',')) {
            if (col == 0) shots = std::stoi(tok);
            if (col == 5) p5 = std::stod(tok);
            if (col == 6) p95 = std::stod(tok);
            ++col;
        }
        if (shots > 0) table[shots] = {p5, p95};
    }
    return table;
}

double C4(long n) {
    const double dn = static_cast<double>(n);
    return 1.0 / (1.0 - 1.0 / (4.0 * dn) - 7.0 / (32.0 * dn * dn) - 19.0 / (128.0 * dn * dn * dn));
}
}

void GenerateShots2D(int count, double meanRadius, std::vector<Shot2D>& out) {
    out.clear();
    out.reserve(count);

    if (count <= 0 || meanRadius <= 0.0) {
        out.assign(static_cast<size_t>(count > 0 ? count : 0), {0.0, 0.0});
        return;
    }

    const double sigma = meanRadius / 1.2533141373155;
    std::normal_distribution<double> norm(0.0, sigma);

    double sumRadius = 0.0;
    for (int i = 0; i < count; ++i) {
        const double x = norm(Rng());
        const double y = norm(Rng());
        out.push_back({x, y});
        sumRadius += std::sqrt(x * x + y * y);
    }

    const double sampledMeanRadius = sumRadius / static_cast<double>(count);
    if (sampledMeanRadius <= 0.0) return;

    const double scale = meanRadius / sampledMeanRadius;
    for (Shot2D& shot : out) {
        shot.x *= scale;
        shot.y *= scale;
    }
}

Stats2D ComputeStats2D(const std::vector<Shot2D>& samples) {
    Stats2D s{};
    if (samples.empty()) return s;
    std::vector<double> radii;
    radii.reserve(samples.size());
    double sum = 0.0;
    for (const Shot2D& p : samples) {
        const double r = std::sqrt(p.x * p.x + p.y * p.y);
        radii.push_back(r);
        sum += r;
    }
    s.meanRadius = sum / samples.size();
    s.p5Radius = Quantile(radii, 0.05);
    s.p95Radius = Quantile(radii, 0.95);
    return s;
}

bool LookupRayleighPercentiles(int shots, double& p5, double& p95) {
    const auto& table = RayleighTable();
    auto it = table.find(shots);
    if (it == table.end()) return false;
    p5 = it->second.first;
    p95 = it->second.second;
    return true;
}

bool CalculateRayleighPercentilesOnDemand(int shots, double& p5Factor, double& p95Factor, ProgressCallback progress, void* userData) {
    if (shots < 2) return false;

    constexpr int kIterations = 50000;
    std::normal_distribution<double> norm(0.0, 1.0);
    std::vector<double> estimates;
    estimates.reserve(kIterations);
    std::vector<double> x(static_cast<size_t>(shots));
    std::vector<double> y(static_cast<size_t>(shots));

    const double c4_2n1 = C4(2L * shots - 1L);
    const double cBessel = static_cast<double>(shots) / (shots - 1.0);
    const auto start = std::chrono::steady_clock::now();
    int lastPercentReported = -1;

    for (int iter = 0; iter < kIterations; ++iter) {
        for (int i = 0; i < shots; ++i) {
            x[static_cast<size_t>(i)] = norm(Rng());
            y[static_cast<size_t>(i)] = norm(Rng());
        }

        const double xBar = std::accumulate(x.begin(), x.end(), 0.0) / shots;
        const double yBar = std::accumulate(y.begin(), y.end(), 0.0) / shots;

        double centerR2Estimate = 0.0;
        for (int i = 0; i < shots; ++i) {
            const double dx = x[static_cast<size_t>(i)] - xBar;
            const double dy = y[static_cast<size_t>(i)] - yBar;
            centerR2Estimate += dx * dx + dy * dy;
        }

        centerR2Estimate /= (2.0 * shots) / cBessel;
        estimates.push_back(std::sqrt(centerR2Estimate) * c4_2n1);

        if (progress) {
            const int percent = ((iter + 1) * 100) / kIterations;
            if (percent != lastPercentReported) {
                const auto now = std::chrono::steady_clock::now();
                const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
                int etaSeconds = 0;
                if (iter + 1 < kIterations && elapsedMs > 0) {
                    const double perIterationMs = static_cast<double>(elapsedMs) / (iter + 1);
                    const double remainingMs = perIterationMs * (kIterations - (iter + 1));
                    etaSeconds = static_cast<int>(std::lround(remainingMs / 1000.0));
                }
                progress(percent, etaSeconds, userData);
                lastPercentReported = percent;
            }
        }
    }

    std::sort(estimates.begin(), estimates.end());
    p5Factor = estimates[(5 * kIterations / 100) - 1];
    p95Factor = estimates[(95 * kIterations / 100) - 1];
    return true;
}
