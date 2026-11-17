#include "tracyx/Comparison.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <tuple>
#include <vector>

namespace tracyx
{
namespace
{

struct Samples
{
    std::vector<uint64_t> frame;
    std::vector<uint64_t> present;
    std::vector<uint64_t> gpuWork;
};

Samples Collect( const Session& session )
{
    Samples samples;
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> anchors;
    for( const auto& event : session.events )
    {
        if( event.process != session.manifest.process ) continue;
        if( event.kind == EventKind::FrameAnchor && event.normalizedNs && !event.frameLinks.empty() )
            anchors.emplace_back( event.frameLinks.front().seriesId, event.frameLinks.front().frameId, *event.normalizedNs );
        else if( event.source == Source::PresentMon && event.kind == EventKind::Present && event.displayIntervalNs )
            samples.present.push_back( *event.displayIntervalNs );
        else if( event.kind == EventKind::GpuWork && event.commandListId != 0 &&
            event.normalizedNs && event.normalizedEndNs &&
            *event.normalizedEndNs >= *event.normalizedNs )
            samples.gpuWork.push_back( *event.normalizedEndNs - *event.normalizedNs );
    }
    std::sort( anchors.begin(), anchors.end() );
    for( size_t index = 1; index < anchors.size(); ++index )
    {
        const auto [series, frame, time] = anchors[index];
        const auto [previousSeries, previousFrame, previousTime] = anchors[index - 1];
        if( series == previousSeries && frame == previousFrame + 1 && time > previousTime )
            samples.frame.push_back( time - previousTime );
    }
    return samples;
}

MetricDistribution Distribution( std::vector<uint64_t> values )
{
    MetricDistribution result;
    result.count = values.size();
    if( values.empty() ) return result;
    std::sort( values.begin(), values.end() );
    result.meanNs = std::accumulate( values.begin(), values.end(), 0.0 ) / values.size();
    const auto percentile = [&]( double fraction ) {
        const auto rank = static_cast<size_t>( std::ceil( fraction * values.size() ) );
        return values[std::max<size_t>( 1, rank ) - 1];
    };
    result.p50Ns = percentile( 0.50 );
    result.p95Ns = percentile( 0.95 );
    result.p99Ns = percentile( 0.99 );
    return result;
}

MetricComparison Metric( std::vector<uint64_t> before, std::vector<uint64_t> after )
{
    MetricComparison result;
    result.before = Distribution( std::move( before ) );
    result.after = Distribution( std::move( after ) );
    if( result.before.p95Ns != 0 )
        result.p95ChangePercent = 100.0 * ( static_cast<double>( result.after.p95Ns ) / result.before.p95Ns - 1.0 );
    if( result.before.p99Ns != 0 )
        result.p99ChangePercent = 100.0 * ( static_cast<double>( result.after.p99Ns ) / result.before.p99Ns - 1.0 );
    return result;
}

bool HasLoss( const SessionManifest& manifest )
{
    return manifest.lostEvents || manifest.queueDrops || manifest.bridgeMissing || manifest.d3d12DecodeErrors ||
        manifest.dxgkDecodeErrors;
}

} // namespace

SessionComparison CompareSessions( const Session& before, const Session& after )
{
    SessionComparison result;
    const auto baseline = Collect( before );
    const auto current = Collect( after );
    result.frame = Metric( baseline.frame, current.frame );
    result.present = Metric( baseline.present, current.present );
    result.gpuWork = Metric( baseline.gpuWork, current.gpuWork );

    const auto conditions = [&]( const char* name, const std::string& a, const std::string& b, bool mayDiffer ) {
        if( a.empty() || b.empty() ) result.warnings.emplace_back( std::string( name ) + " metadata missing" );
        else if( !mayDiffer && a != b ) result.warnings.emplace_back( std::string( name ) + " differs" );
    };
    conditions( "Build SHA", before.manifest.buildSha, after.manifest.buildSha, true );
    conditions( "Environment", before.manifest.environment, after.manifest.environment, false );
    conditions( "GPU", before.manifest.gpuName, after.manifest.gpuName, false );
    conditions( "Driver", before.manifest.driverVersion, after.manifest.driverVersion, false );
    conditions( "Scene", before.manifest.scene, after.manifest.scene, false );
    conditions( "Mode", before.manifest.captureMode, after.manifest.captureMode, false );
    if( HasLoss( before.manifest ) || HasLoss( after.manifest ) )
        result.warnings.emplace_back( "One or both sessions have dropped or undecoded events" );
    if( before.manifest.presentMonDropped || after.manifest.presentMonDropped )
        result.warnings.emplace_back( "One or both sessions include dropped presents" );
    if( before.manifest.clockResidualNs > 1'000'000 || after.manifest.clockResidualNs > 1'000'000 )
        result.warnings.emplace_back( "Clock residual exceeds 1 ms" );
    if( result.frame.before.count < 30 || result.frame.after.count < 30 )
        result.warnings.emplace_back( "Frame sample count below 30" );
    if( result.present.before.count == 0 || result.present.after.count == 0 )
        result.warnings.emplace_back( "Present interval unavailable in one or both sessions" );
    if( result.gpuWork.before.count == 0 || result.gpuWork.after.count == 0 )
        result.warnings.emplace_back( "GPU work duration unavailable in one or both sessions" );
    return result;
}

} // namespace tracyx
