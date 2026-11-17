#pragma once

#include "tracyx/Session.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tracyx
{

enum class DiagnosticKind : uint8_t
{
    PsoCompile,
    Residency,
    QueueWait
};

struct FrameDiagnostic
{
    DiagnosticKind kind = DiagnosticKind::PsoCompile;
    uint64_t evidenceEventId = 0;
    uint64_t relatedEventId = 0;
    uint64_t overlapNs = 0;
    uint64_t uncertaintyNs = 0;
    std::string observation;
};

struct FrameDiagnosticReport
{
    uint64_t seriesId = 0;
    uint64_t frameId = 0;
    uint64_t beginNs = 0;
    uint64_t endNs = 0;
    bool hasFrameWindow = false;
    bool degradedQuality = false;
    std::vector<FrameDiagnostic> candidates;
};

FrameDiagnosticReport AnalyzeFrameDiagnostics( const Session& session, uint64_t seriesId, uint64_t frameId );

} // namespace tracyx
