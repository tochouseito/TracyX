#pragma once

#include "tracyx/Session.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace tracyx
{

struct MetricDistribution
{
    size_t count = 0;
    double meanNs = 0;
    uint64_t p50Ns = 0;
    uint64_t p95Ns = 0;
    uint64_t p99Ns = 0;
};

struct MetricComparison
{
    MetricDistribution before;
    MetricDistribution after;
    double p95ChangePercent = 0;
    double p99ChangePercent = 0;
};

struct SessionComparison
{
    MetricComparison frame;
    MetricComparison present;
    MetricComparison gpuWork;
    std::vector<std::string> warnings;
};

SessionComparison CompareSessions( const Session& before, const Session& after );

} // namespace tracyx
