#include "TracyXPanel.hpp"

#include "tracyx/Clock.hpp"
#include "tracyx/Correlation.hpp"

#include "../profiler/TracyViewData.hpp"
#include "../../../server/TracyWorker.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#endif

namespace tracy::tracyx_viewer
{
namespace
{

std::optional<uint64_t> ToQpc( uint64_t timestamp, tracyx::ClockDomain clock, const tracyx::SessionManifest& manifest )
{
    if( clock == tracyx::ClockDomain::WindowsQpc ) return timestamp;
    if( clock != tracyx::ClockDomain::DxTimingNs || manifest.qpcFrequency == 0 ) return std::nullopt;
    const auto ticks = std::round( static_cast<long double>( timestamp ) * manifest.qpcFrequency / 1'000'000'000.0L );
    if( ticks < 0 || ticks > static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) return std::nullopt;
    return static_cast<uint64_t>( ticks );
}

const char* KindName( tracyx::EventKind kind )
{
    switch( kind )
    {
    case tracyx::EventKind::FrameAnchor: return "Frame anchor";
    case tracyx::EventKind::GpuWork: return "GPU work";
    case tracyx::EventKind::QueueSubmit: return "Queue submit";
    case tracyx::EventKind::Present: return "Present";
    case tracyx::EventKind::PsoCompile: return "PSO compile";
    case tracyx::EventKind::Residency: return "Residency";
    case tracyx::EventKind::RuntimeError: return "Runtime error";
    case tracyx::EventKind::CustomCounter: return "Counter";
    default: return "Unknown";
    }
}

const char* SourceName( tracyx::Source source )
{
    switch( source )
    {
    case tracyx::Source::Bridge: return "Frame Bridge";
    case tracyx::Source::DxEtw: return "DX ETW";
    case tracyx::Source::PresentMon: return "PresentMon";
    case tracyx::Source::Pix: return "PIX";
    default: return "Unknown";
    }
}

const char* ClockName( tracyx::ClockDomain clock )
{
    switch( clock )
    {
    case tracyx::ClockDomain::WindowsQpc: return "Windows QPC ticks";
    case tracyx::ClockDomain::DxTimingNs: return "DxTiming absolute ns";
    case tracyx::ClockDomain::Tracy: return "Tracy ns";
    case tracyx::ClockDomain::Gpu: return "GPU ticks";
    case tracyx::ClockDomain::PresentMon: return "PresentMon";
    default: return "Unknown";
    }
}

} // namespace

Panel::~Panel()
{
    m_liveRunning.store( false );
#ifdef _WIN32
    if( m_liveThread.joinable() ) CancelSynchronousIo( m_liveThread.native_handle() );
#endif
    if( m_liveThread.joinable() ) m_liveThread.join();
}

void Panel::StartLive( uint32_t pid )
{
#ifdef _WIN32
    if( m_livePid != 0 || pid == 0 ) return;
    m_livePid = pid;
    m_liveRunning.store( true );
    m_liveThread = std::thread( [this, pid]() {
        const auto name = std::wstring( L"\\\\.\\pipe\\TracyX." ) + std::to_wstring( pid );
        while( m_liveRunning.load() )
        {
            if( !WaitNamedPipeW( name.c_str(), 500 ) )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
                continue;
            }
            const auto pipe = CreateFileW( name.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr );
            if( pipe == INVALID_HANDLE_VALUE ) continue;
            while( m_liveRunning.load() )
            {
                tracyx::WireBatch batch;
                auto* bytes = reinterpret_cast<char*>( &batch );
                DWORD total = 0;
                while( total < sizeof( batch ) && m_liveRunning.load() )
                {
                    DWORD read = 0;
                    if( !ReadFile( pipe, bytes + total, static_cast<DWORD>( sizeof( batch ) - total ), &read, nullptr ) || read == 0 ) break;
                    total += read;
                }
                if( total != sizeof( batch ) || batch.magic != tracyx::LiveMagic ||
                    batch.version != tracyx::LiveVersion || batch.count > tracyx::LiveBatchCapacity || batch.processId != pid ) break;
                m_liveDisconnected.store( false );
                std::lock_guard lock( m_liveMutex );
                if( m_liveQueue.size() >= 64 ) { m_viewerDrops += batch.count; continue; }
                m_liveQueue.push_back( std::move( batch ) );
            }
            CloseHandle( pipe );
            if( m_liveRunning.load() ) m_liveDisconnected.store( true );
        }
    } );
#else
    (void)pid;
#endif
}

void Panel::PollLive( Worker& worker, const FrameData* frames )
{
    std::deque<tracyx::WireBatch> pending;
    {
        std::lock_guard lock( m_liveMutex );
        pending.swap( m_liveQueue );
    }
    for( const auto& batch : pending )
    {
        const tracyx::ProcessIdentity process{ batch.processId, batch.processStart100ns };
        if( m_session.manifest.process.processId != 0 && m_session.manifest.process != process )
        {
            m_session = {};
            m_liveLastEventId = 0;
            m_liveLastAnchorCount = 0;
        }
        m_session.manifest.process = process;
        m_session.manifest.qpcFrequency = batch.qpcFrequency;
        m_session.manifest.qpcOrigin = batch.qpcOrigin;
        m_session.manifest.lostEvents = batch.lostEvents;
        m_session.manifest.queueDrops = batch.queueDrops;
        m_session.manifest.bridgeMissing = batch.bridgeMissing;
        m_session.manifest.d3d12Events = batch.d3d12Events;
        m_session.manifest.dxgkEvents = batch.dxgkEvents;
        m_session.manifest.d3d12DecodeErrors = batch.d3d12DecodeErrors;
        m_session.manifest.dxgkDecodeErrors = batch.dxgkDecodeErrors;
        m_session.manifest.presentMonState = batch.presentMonState;
        m_session.manifest.presentMonFrames = batch.presentMonFrames;
        m_session.manifest.presentMonDropped = batch.presentMonDropped;
        m_session.manifest.presentMonError = batch.presentMonError;
        m_liveBacklog = batch.backlog;
        m_loaded = true;
        for( uint32_t i = 0; i < batch.count; ++i )
        {
            if( batch.events[i].eventId <= m_liveLastEventId ) continue;
            const auto event = tracyx::FromWire( batch.events[i] );
            if( event.kind == tracyx::EventKind::FrameAnchor ) ++m_liveLastAnchorCount;
            m_session.events.push_back( event );
            m_liveLastEventId = event.eventId;
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if( frames && m_liveLastAnchorCount >= 2 && m_session.events.size() != m_lastAlignedCount &&
        now - m_lastAlignment >= std::chrono::milliseconds( 500 ) )
    {
        Align( worker, frames );
        m_lastAlignedCount = m_session.events.size();
        m_lastAlignment = now;
    }
    else if( !pending.empty() && m_anchorCount < 2 ) m_status = "Live Agent connected; waiting for FrameMark anchors";
    if( pending.empty() && m_liveDisconnected.load() && m_loaded ) m_status = "Live Agent disconnected; saved session may be available";
}

void Panel::Load( const std::filesystem::path& directory )
{
    tracyx::Session loaded;
    std::string error;
    if( !tracyx::LoadSession( directory, loaded, error ) )
    {
        m_status = error;
        m_loaded = false;
        m_aligned.clear();
        return;
    }
    if( !loaded.manifest.tracyFile.empty() && !std::filesystem::exists( directory / loaded.manifest.tracyFile ) )
    {
        m_status = "The referenced .tracy file is missing";
        m_loaded = false;
        return;
    }
    m_session = std::move( loaded );
    m_loaded = true;
    m_status = "Session loaded";
    m_aligned.clear();
    m_frameIdByIndex.clear();
    m_anchorCount = 0;
    m_selected = 0;
    m_seriesId = 0;
}

void Panel::Align( Worker& worker, const FrameData* frames )
{
    m_lastFrames = frames;
    m_aligned.clear();
    m_frameIdByIndex.clear();
    m_unaligned = 0;
    m_processMismatch = 0;
    m_unconvertibleClock = 0;
    m_outsideAnchors = 0;
    m_anchorCount = 0;
    if( !frames || !m_loaded ) return;
    if( worker.GetPid() != m_session.manifest.process.processId )
    {
        m_status = "Tracy and ETW process IDs differ; alignment is disabled";
        return;
    }

    tracyx::TracyClockFit fit( m_session.manifest.qpcFrequency );
    const auto frameCount = worker.GetFrameCount( *frames );
    std::vector<const tracyx::Event*> anchors;
    for( const auto& event : m_session.events )
    {
        if( event.kind == tracyx::EventKind::FrameAnchor && !event.frameLinks.empty() && event.rawEndTimestamp &&
            event.frameLinks.front().seriesId == m_seriesId ) anchors.push_back( &event );
    }
    std::sort( anchors.begin(), anchors.end(), []( const auto* a, const auto* b ) {
        return a->frameLinks.front().frameId < b->frameLinks.front().frameId;
    } );
    const auto matchCount = std::min<size_t>( { frameCount, anchors.size(), 128 } );
    if( matchCount < 2 || m_session.manifest.qpcFrequency == 0 )
    {
        m_status = "Not enough frame anchors for this capture";
        return;
    }

    // Capture は計測対象の起動後に接続できるため、先頭フレームの欠落を間隔で検出する。
    const auto sampleCount = std::min<size_t>( matchCount - 1, 48 );
    long double bestScore = std::numeric_limits<long double>::max();
    size_t bestOffset = 0;
    for( size_t offset = 0; offset + matchCount <= anchors.size(); ++offset )
    {
        std::vector<long double> deviations;
        deviations.reserve( sampleCount );
        for( size_t i = 0; i < sampleCount; ++i )
        {
            const auto left = anchors[offset + i];
            const auto right = anchors[offset + i + 1];
            if( right->frameLinks.front().frameId != left->frameLinks.front().frameId + 1 )
            {
                deviations.clear();
                break;
            }
            const auto qpcDelta = static_cast<long double>( right->rawTimestamp - left->rawTimestamp ) *
                1'000'000'000.0L / m_session.manifest.qpcFrequency;
            const auto tracyDelta = worker.GetFrameBegin( *frames, i + 1 ) - worker.GetFrameBegin( *frames, i );
            deviations.push_back( std::abs( qpcDelta - tracyDelta ) );
        }
        if( deviations.size() != sampleCount ) continue;
        std::sort( deviations.begin(), deviations.end() );
        long double score = 0;
        for( size_t i = 0; i < deviations.size() - std::min<size_t>( 2, deviations.size() - 1 ); ++i ) score += deviations[i];
        if( score < bestScore ) { bestScore = score; bestOffset = offset; }
    }
    if( !std::isfinite( bestScore ) || bestScore / std::max<size_t>( 1, sampleCount - 2 ) > 2'000'000.0L )
    {
        m_status = "Frame sequence does not match QPC anchors";
        return;
    }
    const auto mappedCount = std::min( frameCount, anchors.size() - bestOffset );
    m_frameIdByIndex.resize( frameCount );
    size_t firstGood = 0;
    while( firstGood + 1 < mappedCount )
    {
        const auto qpcDelta = static_cast<long double>( anchors[bestOffset + firstGood + 1]->rawTimestamp -
            anchors[bestOffset + firstGood]->rawTimestamp ) * 1'000'000'000.0L / m_session.manifest.qpcFrequency;
        const auto tracyDelta = worker.GetFrameBegin( *frames, firstGood + 1 ) - worker.GetFrameBegin( *frames, firstGood );
        if( std::abs( qpcDelta - tracyDelta ) <= 2'000'000.0L ) break;
        ++firstGood;
    }
    for( size_t i = firstGood; i < mappedCount; ++i )
    {
        const auto* anchor = anchors[bestOffset + i];
        if( i > firstGood && anchor->frameLinks.front().frameId != anchors[bestOffset + i - 1]->frameLinks.front().frameId + 1 ) break;
        const auto begin = worker.GetFrameBegin( *frames, i );
        if( begin < 0 ) continue;
        m_frameIdByIndex[i] = anchor->frameLinks.front().frameId;
        if( fit.AddAnchor( { anchor->rawTimestamp, *anchor->rawEndTimestamp, static_cast<uint64_t>( begin ) } ) ) ++m_anchorCount;
    }
    if( m_anchorCount < 2 )
    {
        m_status = "At least two matching FrameMark anchors are required";
        return;
    }
    m_residual = fit.MaxResidualNs();

    tracyx::FrameCorrelator correlator;
    for( size_t index = firstGood; index < mappedCount; ++index )
    {
        const auto begin = worker.GetFrameBegin( *frames, index );
        const auto end = worker.GetFrameEnd( *frames, index );
        if( begin >= 0 && end >= begin ) correlator.AddFrame( { anchors[bestOffset + index]->frameLinks.front().frameId,
            m_session.manifest.process, static_cast<uint64_t>( begin ), static_cast<uint64_t>( end ), m_seriesId } );
    }

    for( size_t index = 0; index < m_session.events.size(); ++index )
    {
        const auto& event = m_session.events[index];
        if( event.kind == tracyx::EventKind::FrameAnchor ) continue;
        if( event.process != m_session.manifest.process ) { ++m_unaligned; ++m_processMismatch; continue; }
        const auto qpcBegin = ToQpc( event.rawTimestamp, event.clock, m_session.manifest );
        const auto qpcEnd = event.rawEndTimestamp ? ToQpc( *event.rawEndTimestamp, event.clock, m_session.manifest ) : qpcBegin;
        if( !qpcBegin || !qpcEnd ) { ++m_unaligned; ++m_unconvertibleClock; continue; }
        const auto begin = fit.ToTracyNs( *qpcBegin );
        const auto end = fit.ToTracyNs( *qpcEnd );
        if( !begin || !end || end->nanoseconds < begin->nanoseconds ||
            end->nanoseconds > static_cast<uint64_t>( std::numeric_limits<int64_t>::max() ) )
        {
            ++m_unaligned;
            ++m_outsideAnchors;
            continue;
        }
        tracyx::Event mapped = event;
        mapped.normalizedNs = begin->nanoseconds;
        mapped.normalizedEndNs = end->nanoseconds;
        mapped.uncertaintyNs += std::max( begin->uncertaintyNs, end->uncertaintyNs );
        m_aligned.push_back( { index, static_cast<int64_t>( begin->nanoseconds ),
            static_cast<int64_t>( end->nanoseconds ), mapped.uncertaintyNs, correlator.FindLinks( mapped ) } );
    }
    std::sort( m_aligned.begin(), m_aligned.end(), []( const auto& a, const auto& b ) { return a.begin < b.begin; } );
    m_status = "Tracy and ETW timestamps aligned";
}

void Panel::Draw( const std::string& traceFile, Worker& worker, const FrameData* frames,
    int64_t viewBegin, int64_t viewEnd, int frameHover,
    const std::function<void( int64_t, int64_t )>& zoom )
{
    if( traceFile.empty() )
    {
        StartLive( static_cast<uint32_t>( worker.GetPid() ) );
        PollLive( worker, frames );
    }
    if( traceFile != m_traceFile )
    {
        m_traceFile = traceFile;
        m_autoLoadChecked = false;
    }
    if( !m_autoLoadChecked && !traceFile.empty() )
    {
        m_autoLoadChecked = true;
        const auto directory = std::filesystem::path( traceFile ).parent_path();
        if( std::filesystem::exists( directory / "session.json" ) )
        {
            const auto path = directory.u8string();
            if( path.size() < m_path.size() ) std::memcpy( m_path.data(), path.c_str(), path.size() + 1 );
            Load( directory );
            if( m_loaded ) Align( worker, frames );
        }
    }
    if( m_loaded && frames != m_lastFrames ) Align( worker, frames );

    if( traceFile.empty() && !m_livePositioned )
    {
        ImGui::SetNextWindowPos( { 300, 90 }, ImGuiCond_Always );
        ImGui::SetNextWindowSize( { 650, 180 }, ImGuiCond_Always );
        m_livePositioned = true;
    }
    if( !traceFile.empty() && !m_savedPositioned )
    {
        ImGui::SetNextWindowPos( { 300, 90 }, ImGuiCond_Always );
        ImGui::SetNextWindowSize( { 850, 420 }, ImGuiCond_Always );
        m_savedPositioned = true;
    }
    ImGui::Begin( "TracyX DX" );
    ImGui::InputText( "Session directory", m_path.data(), m_path.size() );
    ImGui::SameLine();
    if( ImGui::Button( "Load" ) )
    {
        Load( std::filesystem::path( m_path.data() ) );
        if( m_loaded ) Align( worker, frames );
    }
    if( m_loaded && m_anchorCount == 0 && frames && worker.GetFrameCount( *frames ) >= 2 ) Align( worker, frames );
    ImGui::TextUnformatted( m_status.c_str() );
    if( !m_loaded ) { ImGui::End(); return; }
    std::vector<uint64_t> seriesIds;
    for( const auto& event : m_session.events )
    {
        if( event.kind != tracyx::EventKind::FrameAnchor || event.frameLinks.empty() ) continue;
        const auto seriesId = event.frameLinks.front().seriesId;
        if( std::find( seriesIds.begin(), seriesIds.end(), seriesId ) == seriesIds.end() ) seriesIds.push_back( seriesId );
    }
    std::sort( seriesIds.begin(), seriesIds.end() );
    if( !seriesIds.empty() && std::find( seriesIds.begin(), seriesIds.end(), m_seriesId ) == seriesIds.end() )
    {
        m_seriesId = seriesIds.front();
        Align( worker, frames );
    }
    if( seriesIds.size() > 1 && ImGui::BeginCombo( "Frame series", std::to_string( m_seriesId ).c_str() ) )
    {
        for( const auto seriesId : seriesIds )
        {
            const auto label = std::to_string( seriesId );
            if( ImGui::Selectable( label.c_str(), seriesId == m_seriesId ) )
            {
                m_seriesId = seriesId;
                Align( worker, frames );
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Text( "External events: %zu | aligned: %zu | outside anchors: %zu | anchors: %zu | fit residual: %llu ns",
        m_session.events.size(), m_aligned.size(), m_unaligned, m_anchorCount,
        static_cast<unsigned long long>( m_residual ) );
    ImGui::Text( "ETW losses: %llu | Agent drops: %llu | Bridge missing: %llu",
        static_cast<unsigned long long>( m_session.manifest.lostEvents ),
        static_cast<unsigned long long>( m_session.manifest.queueDrops ),
        static_cast<unsigned long long>( m_session.manifest.bridgeMissing ) );
    ImGui::Text( "D3D12 records: %llu (decode errors %llu) | DXGK records: %llu (decode errors %llu)",
        static_cast<unsigned long long>( m_session.manifest.d3d12Events ),
        static_cast<unsigned long long>( m_session.manifest.d3d12DecodeErrors ),
        static_cast<unsigned long long>( m_session.manifest.dxgkEvents ),
        static_cast<unsigned long long>( m_session.manifest.dxgkDecodeErrors ) );
    ImGui::TextUnformatted( "ETW does not attribute session buffer losses to an individual provider." );
    ImGui::Text( "PresentMon: %s | presents: %llu | dropped: %llu",
        m_session.manifest.presentMonState == 1 ? "active" : m_session.manifest.presentMonState == 2 ? "failed" : "unavailable",
        static_cast<unsigned long long>( m_session.manifest.presentMonFrames ),
        static_cast<unsigned long long>( m_session.manifest.presentMonDropped ) );
    if( !m_session.manifest.presentMonError.empty() ) ImGui::TextUnformatted( m_session.manifest.presentMonError.c_str() );
    if( m_unaligned )
        ImGui::Text( "Unaligned: outside anchors %zu | process mismatch %zu | unconvertible clock %zu",
            m_outsideAnchors, m_processMismatch, m_unconvertibleClock );
    if( m_livePid != 0 ) ImGui::Text( "Pipe backlog: %llu | Viewer drops: %llu",
        static_cast<unsigned long long>( m_liveBacklog ), static_cast<unsigned long long>( m_viewerDrops.load() ) );
    if( m_livePid != 0 ) ImGui::Text( "Bridge anchors received: %zu", m_liveLastAnchorCount );
    const auto hoverFrameId = frameHover >= 0 && static_cast<size_t>( frameHover ) < m_frameIdByIndex.size()
        ? m_frameIdByIndex[frameHover] : 0;
    if( m_hovered > 0 && m_hovered <= m_session.events.size() )
    {
        if( ImGui::Button( "Inspect hovered event" ) ) m_selected = m_hovered;
        ImGui::SameLine();
        ImGui::Text( "Event %llu", static_cast<unsigned long long>( m_session.events[m_hovered - 1].eventId ) );
    }

    if( viewEnd > viewBegin && !m_aligned.empty() )
    {
        const auto width = std::max( 1.0f, ImGui::GetContentRegionAvail().x );
        constexpr float rowHeight = 22.0f;
        const auto first = m_aligned.begin();
        using Lane = std::tuple<uint8_t, uint64_t, uint64_t>;
        std::vector<Lane> lanes;
        for( auto it = first; it != m_aligned.end() && it->begin < viewEnd; ++it )
        {
            if( it->end < viewBegin ) continue;
            const auto& event = m_session.events[it->index];
            const Lane lane = event.source == tracyx::Source::PresentMon
                ? Lane{ 1, event.swapChainId, 0 } : Lane{ 0, event.queueId, 0 };
            if( std::find( lanes.begin(), lanes.end(), lane ) == lanes.end() ) lanes.push_back( lane );
        }
        if( m_selected > 0 && m_selected <= m_session.events.size() )
        {
            const auto& selected = m_session.events[m_selected - 1];
            if( selected.source != tracyx::Source::PresentMon && selected.commandListId != 0 )
                lanes.push_back( { 0, selected.queueId, selected.commandListId } );
        }
        std::sort( lanes.begin(), lanes.end() );
        lanes.erase( std::unique( lanes.begin(), lanes.end() ), lanes.end() );
        const auto omittedLanes = lanes.size() > 48 ? lanes.size() - 48 : 0;
        if( omittedLanes ) lanes.resize( 48 );
        if( omittedLanes ) ImGui::Text( "%zu lanes omitted", omittedLanes );
        const auto origin = ImGui::GetCursorScreenPos();
        const auto height = rowHeight * std::max<size_t>( 1, lanes.size() );
        ImGui::InvisibleButton( "DX lanes", { width, height } );
        const auto laneClicked = ImGui::IsItemClicked( ImGuiMouseButton_Left );
        auto* draw = ImGui::GetWindowDrawList();
        const auto span = static_cast<double>( viewEnd - viewBegin );
        for( size_t row = 0; row < lanes.size(); ++row )
        {
            const auto top = origin.y + row * rowHeight;
            const auto [laneType, laneId, detailId] = lanes[row];
            const auto label = laneType == 1 ? "Present swap chain " + std::to_string( laneId )
                : detailId == 0 ? "DX queue " + std::to_string( laneId )
                : "DX queue " + std::to_string( laneId ) + " / command list " + std::to_string( detailId );
            draw->AddText( { origin.x + 3, top + 2 }, IM_COL32( 220, 220, 220, 255 ),
                label.c_str() );
            draw->AddLine( { origin.x, top + rowHeight - 1 }, { origin.x + width, top + rowHeight - 1 }, IM_COL32( 80, 80, 80, 255 ) );
        }
        size_t drawn = 0;
        for( auto it = first; it != m_aligned.end() && it->begin < viewEnd && drawn < 20'000; ++it )
        {
            if( it->end < viewBegin ) continue;
            ++drawn;
            const auto& event = m_session.events[it->index];
            const Lane lane = event.source == tracyx::Source::PresentMon
                ? Lane{ 1, event.swapChainId, 0 } : Lane{ 0, event.queueId, 0 };
            const auto rowIt = std::lower_bound( lanes.begin(), lanes.end(), lane );
            if( rowIt == lanes.end() || *rowIt != lane ) continue;
            const auto left = origin.x + static_cast<float>( std::clamp( ( it->begin - viewBegin ) / span, 0.0, 1.0 ) * width );
            const auto right = origin.x + std::max( 2.0f, static_cast<float>( std::clamp( ( it->end - viewBegin ) / span, 0.0, 1.0 ) * width ) );
            const auto color = event.source == tracyx::Source::PresentMon
                ? event.displayStatus == tracyx::DisplayStatus::Dropped ? IM_COL32( 225, 90, 90, 230 )
                    : event.displayStatus == tracyx::DisplayStatus::Displayed ? IM_COL32( 90, 205, 135, 230 )
                    : IM_COL32( 150, 150, 150, 230 )
                : event.kind == tracyx::EventKind::PsoCompile ? IM_COL32( 230, 175, 70, 230 ) : IM_COL32( 85, 175, 235, 230 );
            const auto drawInRow = [&]( size_t row ) {
                const auto top = origin.y + row * rowHeight + 3;
                draw->AddRectFilled( { left, top }, { std::max( left + 2, right ), top + rowHeight - 6 }, color );
                if( hoverFrameId && std::any_of( it->frameLinks.begin(), it->frameLinks.end(), [&]( const auto& link ) {
                    return link.seriesId == m_seriesId && link.frameId == hoverFrameId;
                } ) )
                    draw->AddRect( { left, top }, { std::max( left + 2, right ), top + rowHeight - 6 },
                        IM_COL32( 255, 255, 255, 255 ), 0, 0, 2.0f );
                if( ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect( { left, top }, { std::max( left + 2, right ), top + rowHeight - 6 } ) )
                {
                    m_hovered = it->index + 1;
                    ImGui::SetTooltip( "%s | %s | event %llu | swap chain %llu | command list %llu", KindName( event.kind ),
                        event.label.c_str(), static_cast<unsigned long long>( event.eventId ),
                        static_cast<unsigned long long>( event.swapChainId ),
                        static_cast<unsigned long long>( event.commandListId ) );
                    if( laneClicked )
                    {
                        m_selected = it->index + 1;
                        if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                            zoom( it->begin - 1'000'000, std::max( it->end + 1'000'000, it->begin + 2'000'000 ) );
                    }
                }
            };
            drawInRow( static_cast<size_t>( rowIt - lanes.begin() ) );
            if( event.source != tracyx::Source::PresentMon && event.commandListId != 0 && m_selected > 0 && m_selected <= m_session.events.size() &&
                event.queueId == m_session.events[m_selected - 1].queueId &&
                event.commandListId == m_session.events[m_selected - 1].commandListId )
            {
                const Lane detailLane{ 0, event.queueId, event.commandListId };
                const auto detail = std::lower_bound( lanes.begin(), lanes.end(), detailLane );
                if( detail != lanes.end() && *detail == detailLane )
                    drawInRow( static_cast<size_t>( detail - lanes.begin() ) );
            }
        }
        if( drawn == 20'000 ) ImGui::TextUnformatted( "Event drawing limited to 20,000 events in this view." );
    }

    if( m_selected > 0 && m_selected <= m_session.events.size() )
    {
        if( !m_inspectorExpanded )
        {
            ImGui::SetWindowSize( { 750, 360 } );
            m_inspectorExpanded = true;
        }
        const auto& event = m_session.events[m_selected - 1];
        ImGui::SeparatorText( "Event Inspector" );
        ImGui::Text( "%s: %s", KindName( event.kind ), event.label.c_str() );
        ImGui::Text( "Event %llu | PID %u | TID %u | source %s | clock %s",
            static_cast<unsigned long long>( event.eventId ), event.process.processId, event.threadId,
            SourceName( event.source ), ClockName( event.clock ) );
        ImGui::Text( "Raw time %llu - %llu | uncertainty %llu ns",
            static_cast<unsigned long long>( event.rawTimestamp ),
            static_cast<unsigned long long>( event.rawEndTimestamp.value_or( event.rawTimestamp ) ),
            static_cast<unsigned long long>( event.uncertaintyNs ) );
        ImGui::Text( "Device %llu | queue %llu | command list %llu | resource %llu",
            static_cast<unsigned long long>( event.deviceId ), static_cast<unsigned long long>( event.queueId ),
            static_cast<unsigned long long>( event.commandListId ), static_cast<unsigned long long>( event.resourceId ) );
        if( event.source == tracyx::Source::PresentMon )
        {
            ImGui::Text( "Swap chain %llu | display %s", static_cast<unsigned long long>( event.swapChainId ),
                event.displayStatus == tracyx::DisplayStatus::Dropped ? "dropped" :
                event.displayStatus == tracyx::DisplayStatus::Displayed ? "displayed" : "unknown" );
            const auto metric = []( const char* name, const std::optional<uint64_t>& value ) {
                if( value ) ImGui::Text( "%s: %.3f ms", name, *value / 1'000'000.0 );
                else ImGui::Text( "%s: unavailable", name );
            };
            metric( "Display interval", event.displayIntervalNs );
            metric( "GPU busy", event.gpuBusyNs );
            metric( "Display latency", event.displayLatencyNs );
            if( event.rawEndTimestamp ) ImGui::TextUnformatted( "Present to display span is shown in the lane." );
        }
        const auto found = std::find_if( m_aligned.begin(), m_aligned.end(), [&]( const auto& item ) { return item.index == m_selected - 1; } );
        if( found != m_aligned.end() )
        {
            ImGui::Text( "Tracy time %lld - %lld ns | anchor bracket uncertainty %llu ns",
                static_cast<long long>( found->begin ), static_cast<long long>( found->end ),
                static_cast<unsigned long long>( found->uncertainty ) );
            for( const auto& link : found->frameLinks )
            {
                ImGui::Text( "Frame series %llu, frame %llu (%s)",
                    static_cast<unsigned long long>( link.seriesId ), static_cast<unsigned long long>( link.frameId ),
                    link.strength == tracyx::LinkStrength::Explicit ? "explicit" : "time overlap" );
            }
            if( ImGui::Button( "Zoom to event" ) )
                zoom( found->begin - 1'000'000, std::max( found->end + 1'000'000, found->begin + 2'000'000 ) );
        }
        else ImGui::TextUnformatted( "No matching Tracy time in the current anchor range." );
    }
    ImGui::End();
}

} // namespace tracy::tracyx_viewer
