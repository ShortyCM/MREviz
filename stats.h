#pragma once

#include <cstdint>
#include <vector>

struct Shot2D {
    double x;
    double y;
};

struct Stats2D {
    double meanRadius;
    double p5Radius;
    double p95Radius;
};

void GenerateShots2D(int count, double meanRadius, std::vector<Shot2D>& out);
Stats2D ComputeStats2D(const std::vector<Shot2D>& samples);
bool LookupRayleighPercentiles(int shots, double& p5, double& p95);
using ProgressCallback = void(*)(int percent, int etaSeconds, void* userData);
bool CalculateRayleighPercentilesOnDemand(int shots, double& p5Factor, double& p95Factor, ProgressCallback progress = nullptr, void* userData = nullptr);
