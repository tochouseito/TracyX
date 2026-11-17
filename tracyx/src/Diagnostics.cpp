#include "tracyx/Diagnostics.hpp"

#include <algorithm>
#include <unordered_map>

namespace tracyx
{
namespace
{

uint64_t Overlap( uint64_t begin, uint64_t end, uint64_t frameBegin, uint64_t frameEnd )
{
    return std::min( end, frameEnd ) > std::max( begin, frameBegin )
        ? std::min( end, frameEnd ) - std::max( begin, frameBegin ) : 0;
}

} // namespace

FrameDiagnosticReport AnalyzeFrameDiagnostics( const Session& session, uint64_t seriesId, uint64_t frameId )
{
    FrameDiagnosticReport report;
    report.seriesId = seriesId;
    report.frameId = frameId;
    report.degradedQuality = session.manifest.lostEvents != 0 || session.manifest.queueDrops != 0 ||
        session.manifest.bridgeMissing != 0 || session.manifest.d3d12DecodeErrors != 0 ||
        session.manifest.dxgkDecodeErrors != 0 || session.manifest.clockResidualNs > 1'000'000;
    bool foundBegin = false;
    bool foundEnd = false;

    for( const auto& event : session.events )
    {
        if( event.kind != EventKind::FrameAnchor || !event.normalizedNs ) continue;
        for( const auto& link : event.frameLinks )
        {
            if( link.seriesId != seriesId ) continue;
            if( link.frameId == frameId ) { report.beginNs = *event.normalizedNs; foundBegin = true; }
            if( frameId != UINT64_MAX && link.frameId == frameId + 1 )
            { report.endNs = *event.normalizedNs; foundEnd = true; }
        }
    }
    if( !foundBegin || !foundEnd || report.endNs <= report.beginNs ) return report;
    report.hasFrameWindow = true;

    std::unordered_map<uint64_t, const Event*> firstWork;
    for( const auto& event : session.events )
    {
        if( event.kind != EventKind::GpuWork || !event.normalizedNs || event.commandListId == 0 ) continue;
        const auto executionId = event.commandListId >> 16;
        const auto found = firstWork.find( executionId );
        if( found == firstWork.end() || *event.normalizedNs < *found->second->normalizedNs )
            firstWork[executionId] = &event;
    }

    for( const auto& event : session.events )
    {
        if( !event.normalizedNs || event.process != session.manifest.process ) continue;
        const auto begin = *event.normalizedNs;
        if( event.kind == EventKind::PsoCompile || event.kind == EventKind::Residency )
        {
            const auto end = event.normalizedEndNs.value_or( begin );
            if( end < report.beginNs || begin >= report.endNs ) continue;
            const auto overlap = Overlap( begin, std::max( end, begin == UINT64_MAX ? begin : begin + 1 ), report.beginNs, report.endNs );
            report.candidates.push_back( { event.kind == EventKind::PsoCompile ? DiagnosticKind::PsoCompile : DiagnosticKind::Residency,
                event.eventId, 0, overlap, event.uncertaintyNs + session.manifest.clockResidualNs,
                event.kind == EventKind::PsoCompile ? "PSO compilation overlaps frame" : "Residency event overlaps frame" } );
        }
        else if( event.kind == EventKind::QueueSubmit && event.commandListId != 0 )
        {
            const auto work = firstWork.find( event.commandListId );
            if( work == firstWork.end() || *work->second->normalizedNs <= begin ) continue;
            const auto overlap = Overlap( begin, *work->second->normalizedNs, report.beginNs, report.endNs );
            if( overlap == 0 ) continue;
            report.candidates.push_back( { DiagnosticKind::QueueWait, event.eventId, work->second->eventId,
                overlap, std::max( event.uncertaintyNs, work->second->uncertaintyNs ) + session.manifest.clockResidualNs,
                "Queue submit to first GPU work overlaps frame" } );
        }
    }
    std::sort( report.candidates.begin(), report.candidates.end(), []( const auto& a, const auto& b ) {
        return a.overlapNs > b.overlapNs;
    } );
    return report;
}

} // namespace tracyx
