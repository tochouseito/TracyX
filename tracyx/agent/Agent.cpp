// DxTimingCaptureLibrary Basic sample の ETW 設定を参照。元コードは Microsoft MIT License。
#include "tracyx/Clock.hpp"
#include "tracyx/LiveProtocol.hpp"
#include "tracyx/Session.hpp"
#ifdef TRACYX_HAS_PRESENTMON
#include "PresentMonAdapter.hpp"
#endif

#include <DxTimingCaptureLibrary/DxTimingCaptureLibrary.h>
#include <DxTimingCaptureLibrary/EtwProviders.h>
#include <DxTimingCaptureLibrary/NoOpCallbacks.h>

#include <Windows.h>
#include <evntrace.h>
#include <evntcons.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>

using namespace DirectX::Etw;

namespace
{

struct TraceProperties
{
    EVENT_TRACE_PROPERTIES properties{};
    wchar_t loggerName[1024]{};

    TraceProperties()
    {
        properties.Wnode.BufferSize = sizeof( *this );
        properties.Wnode.ClientContext = 1;
        properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        properties.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        properties.LoggerNameOffset = FIELD_OFFSET( TraceProperties, loggerName );
        properties.BufferSize = 256;
        properties.MinimumBuffers = 16;
        properties.MaximumBuffers = 256;
    }
};

struct AgentState
{
    tracyx::Session session;
    tracyx::QpcClock qpcClock;
    uint64_t originNs = 0;
    size_t maxEvents = 1'000'000;
    std::atomic<bool> running{ true };
    std::atomic<uint64_t> nextEventId{ 1 };
    std::atomic<uint64_t> lostEvents{ 0 };
    std::atomic<uint64_t> decodeErrors{ 0 };
    std::atomic<uint64_t> queueDrops{ 0 };
    std::atomic<uint64_t> bridgeMissing{ 0 };
    std::atomic<uint64_t> d3d12Events{ 0 };
    std::atomic<uint64_t> dxgkEvents{ 0 };
    std::atomic<uint64_t> d3d12DecodeErrors{ 0 };
    std::atomic<uint64_t> dxgkDecodeErrors{ 0 };
    std::atomic<uint32_t> presentMonState{ 0 };
    std::atomic<uint64_t> presentMonFrames{ 0 };
    std::atomic<uint64_t> presentMonDropped{ 0 };
    std::string presentMonError;
    std::unordered_map<uint64_t, uint64_t> lastFrameBySeries;
    std::mutex eventMutex;
    std::unique_ptr<DxTimingCaptureEventHandler> handler;

    AgentState( tracyx::ProcessIdentity process, uint64_t frequency, uint64_t origin, size_t eventLimit )
        : qpcClock( frequency, origin )
        , maxEvents( eventLimit )
    {
        originNs = static_cast<uint64_t>( static_cast<long double>( origin ) * 1'000'000'000.0L / frequency );
        session.manifest.process = process;
        session.manifest.qpcFrequency = frequency;
        session.manifest.qpcOrigin = origin;
    }

    void Push( tracyx::Event event )
    {
        event.eventId = nextEventId.fetch_add( 1 );
        if( event.source == tracyx::Source::DxEtw )
        {
            event.clock = tracyx::ClockDomain::DxTimingNs;
            if( event.rawTimestamp >= originNs )
            {
                event.normalizedNs = event.rawTimestamp - originNs;
                event.uncertaintyNs = 1;
            }
            if( event.rawEndTimestamp && *event.rawEndTimestamp >= originNs )
                event.normalizedEndNs = *event.rawEndTimestamp - originNs;
        }
        else if( event.source == tracyx::Source::Bridge || event.source == tracyx::Source::PresentMon )
        {
            if( const auto time = qpcClock.Normalize( event.rawTimestamp ) ) event.normalizedNs = time->nanoseconds;
            if( event.rawEndTimestamp )
                if( const auto time = qpcClock.Normalize( *event.rawEndTimestamp ) ) event.normalizedEndNs = time->nanoseconds;
        }
        std::lock_guard lock( eventMutex );
        if( session.events.size() >= maxEvents )
        {
            queueDrops.fetch_add( 1 );
            return;
        }
        session.events.push_back( std::move( event ) );
    }
};

AgentState* activeAgent = nullptr;

class GpuCallbacks final : public NoOpGpuTimingsCallbacks
{
public:
    explicit GpuCallbacks( AgentState& state ) : m_state( state ) {}

    HRESULT OnGpuExecutionBegin( UINT32 processId, UINT32 threadId, UINT64 apiQueueId,
        INT64 submitTime, UINT64 presentToken, UINT64* executionId ) override
    {
        const auto status = NoOpGpuTimingsCallbacks::OnGpuExecutionBegin(
            processId, threadId, apiQueueId, submitTime, presentToken, executionId );
        m_executions[*executionId] = { processId, threadId };
        if( submitTime >= 0 )
        {
            tracyx::Event event;
            event.kind = tracyx::EventKind::QueueSubmit;
            event.rawTimestamp = static_cast<uint64_t>( submitTime );
            event.process = { processId, m_state.session.manifest.process.startTime100ns };
            event.threadId = threadId;
            event.queueId = apiQueueId;
            event.commandListId = *executionId;
            event.label = "Queue submit";
            m_state.Push( std::move( event ) );
        }
        return status;
    }

    HRESULT OnGpuExecutionComplete( UINT64 executionId ) override
    {
        m_executions.erase( executionId );
        return S_OK;
    }

    HRESULT OnGpuWork( UINT64 hardwareQueueId, UINT64 executionId, UINT32 commandListIndex,
        UINT64, INT64 begin, INT64 end ) override
    {
        if( begin < 0 || end < begin ) return S_OK;
        const auto it = m_executions.find( executionId );
        if( it == m_executions.end() ) return S_OK;
        tracyx::Event event;
        event.kind = tracyx::EventKind::GpuWork;
        event.rawTimestamp = static_cast<uint64_t>( begin );
        event.rawEndTimestamp = static_cast<uint64_t>( end );
        event.process = { it->second.first, m_state.session.manifest.process.startTime100ns };
        event.threadId = it->second.second;
        event.queueId = hardwareQueueId;
        event.commandListId = ( executionId << 16 ) | commandListIndex;
        event.label = "GPU command list";
        m_state.Push( std::move( event ) );
        return S_OK;
    }

private:
    AgentState& m_state;
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> m_executions;
};

class PsoCallbacks final : public NoOpPipelineStateEventCallbacks
{
public:
    explicit PsoCallbacks( AgentState& state ) : m_state( state ) {}

    HRESULT OnPsoCompilation( UINT32 processId, UINT32 threadId, UINT64 apiObjectId,
        INT64 begin, INT64 end ) override
    {
        if( begin < 0 || end < begin ) return S_OK;
        tracyx::Event event;
        event.kind = tracyx::EventKind::PsoCompile;
        event.rawTimestamp = static_cast<uint64_t>( begin );
        event.rawEndTimestamp = static_cast<uint64_t>( end );
        event.process = { processId, m_state.session.manifest.process.startTime100ns };
        event.threadId = threadId;
        event.resourceId = apiObjectId;
        event.label = "PSO compilation";
        m_state.Push( std::move( event ) );
        return S_OK;
    }

private:
    AgentState& m_state;
};

class EngineActivityCallbacks final : public NoOpGpuEngineActivityCallbacks
{
public:
    explicit EngineActivityCallbacks( AgentState& state ) : m_state( state ) {}

    HRESULT OnGpuEngineActivity( UINT32 processId, UINT64 hardwareQueueId, UINT32 engineAffinity,
        INT64 begin, INT64 end, bool hardwareScheduled ) override
    {
        if( processId != m_state.session.manifest.process.processId || begin < 0 || end < begin ) return S_OK;
        tracyx::Event event;
        event.kind = tracyx::EventKind::GpuWork;
        event.rawTimestamp = static_cast<uint64_t>( begin );
        event.rawEndTimestamp = static_cast<uint64_t>( end );
        event.process = m_state.session.manifest.process;
        event.queueId = hardwareQueueId;
        event.deviceId = engineAffinity;
        event.label = hardwareScheduled ? "GPU engine scheduled" : "GPU engine activity upper bound";
        m_state.Push( std::move( event ) );
        return S_OK;
    }

private:
    AgentState& m_state;
};

class DiagnosticCallbacks final : public DiagnosticsSink
{
public:
    explicit DiagnosticCallbacks( AgentState& state ) : m_state( state ) {}

    void OnDiagnostic( DiagnosticSeverity severity, DiagnosticCode code, std::wstring_view message ) override
    {
        std::fwprintf( stderr, L"[DxTiming %d/%d] %.*ls\n", static_cast<int>( severity ),
            static_cast<int>( code ), static_cast<int>( message.size() ), message.data() );
    }

private:
    AgentState& m_state;
};

void WINAPI OnEvent( EVENT_RECORD* record )
{
    const auto d3d12 = IsEqualGUID( record->EventHeader.ProviderId, Direct3D12EtwProviderGuid );
    const auto dxgk = IsEqualGUID( record->EventHeader.ProviderId, DxgkControlGuid );
    if( d3d12 ) activeAgent->d3d12Events.fetch_add( 1 );
    if( dxgk ) activeAgent->dxgkEvents.fetch_add( 1 );
    try { activeAgent->handler->HandleEventRecord( record ); }
    catch( ... )
    {
        activeAgent->decodeErrors.fetch_add( 1 );
        if( d3d12 ) activeAgent->d3d12DecodeErrors.fetch_add( 1 );
        if( dxgk ) activeAgent->dxgkDecodeErrors.fetch_add( 1 );
    }
}

ULONG WINAPI OnBuffer( EVENT_TRACE_LOGFILEW* trace )
{
    activeAgent->handler->ReportTraceStatistics( *trace );
    activeAgent->lostEvents.store( trace->EventsLost );
    return TRUE;
}

BOOL WINAPI OnConsoleSignal( DWORD signal )
{
    if( signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT ) return FALSE;
    activeAgent->running.store( false );
    return TRUE;
}

uint64_t ProcessStartTime( uint32_t pid )
{
    const auto process = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid );
    if( !process ) return 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    const auto ok = GetProcessTimes( process, &created, &exited, &kernel, &user );
    CloseHandle( process );
    if( !ok ) return 0;
    ULARGE_INTEGER time{};
    time.LowPart = created.dwLowDateTime;
    time.HighPart = created.dwHighDateTime;
    return time.QuadPart;
}

ULONG EnableProvider( TRACEHANDLE session, const GUID& guid, ULONGLONG keywords, UCHAR level )
{
    ENABLE_TRACE_PARAMETERS parameters{};
    parameters.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
    const auto enabled = EnableTraceEx2( session, &guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        level, keywords, 0, 5000, &parameters );
    if( enabled != ERROR_SUCCESS ) return enabled;
    // 収集開始前に作られた Queue や Device の識別子を復元する。
    return EnableTraceEx2( session, &guid, EVENT_CONTROL_CODE_CAPTURE_STATE,
        TRACE_LEVEL_VERBOSE, 0, 0, INFINITE, nullptr );
}

void ReadAnchors( AgentState& state, const std::filesystem::path& file, size_t& next )
{
    std::vector<tracyx::FrameAnchorRecord> anchors;
    std::string error;
    if( !tracyx::LoadFrameAnchors( file, anchors, error ) ) return;
    for( ; next < anchors.size(); ++next )
    {
        const auto& anchor = anchors[next];
        auto& previous = state.lastFrameBySeries[anchor.seriesId];
        if( previous != 0 && anchor.frameId > previous + 1 )
            state.bridgeMissing.fetch_add( anchor.frameId - previous - 1 );
        previous = anchor.frameId;
        tracyx::Event event;
        event.source = tracyx::Source::Bridge;
        event.kind = tracyx::EventKind::FrameAnchor;
        event.clock = tracyx::ClockDomain::WindowsQpc;
        event.rawTimestamp = anchor.qpcBefore;
        event.rawEndTimestamp = anchor.qpcAfter;
        event.process = state.session.manifest.process;
        event.frameLinks.push_back( { anchor.frameId, tracyx::LinkStrength::Explicit, anchor.seriesId } );
        event.label = "FrameMark QPC anchor";
        state.Push( std::move( event ) );
    }
}

void StreamBatches( AgentState& state )
{
    const auto name = std::wstring( L"\\\\.\\pipe\\TracyX." ) +
        std::to_wstring( state.session.manifest.process.processId );
    while( state.running.load() )
    {
        const auto pipe = CreateNamedPipeW( name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 64 * 1024, 64 * 1024, 0, nullptr );
        if( pipe == INVALID_HANDLE_VALUE )
        {
            std::fwprintf( stderr, L"CreateNamedPipeW failed: %lu\n", GetLastError() );
            return;
        }
        const auto connected = ConnectNamedPipe( pipe, nullptr ) || GetLastError() == ERROR_PIPE_CONNECTED;
        if( !connected || !state.running.load() ) { CloseHandle( pipe ); continue; }
        size_t next = 0;
        while( state.running.load() )
        {
            tracyx::WireBatch batch;
            batch.qpcFrequency = state.session.manifest.qpcFrequency;
            batch.qpcOrigin = state.session.manifest.qpcOrigin;
            batch.processId = state.session.manifest.process.processId;
            batch.processStart100ns = state.session.manifest.process.startTime100ns;
            batch.lostEvents = state.lostEvents.load();
            batch.queueDrops = state.queueDrops.load();
            batch.bridgeMissing = state.bridgeMissing.load();
            batch.d3d12Events = state.d3d12Events.load();
            batch.dxgkEvents = state.dxgkEvents.load();
            batch.d3d12DecodeErrors = state.d3d12DecodeErrors.load();
            batch.dxgkDecodeErrors = state.dxgkDecodeErrors.load();
            batch.presentMonState = state.presentMonState.load();
            batch.presentMonFrames = state.presentMonFrames.load();
            batch.presentMonDropped = state.presentMonDropped.load();
            {
                std::lock_guard lock( state.eventMutex );
                std::memcpy( batch.presentMonError, state.presentMonError.data(),
                    std::min( state.presentMonError.size(), sizeof( batch.presentMonError ) - 1 ) );
            }
            {
                std::lock_guard lock( state.eventMutex );
                batch.count = static_cast<uint32_t>( std::min<size_t>( tracyx::LiveBatchCapacity,
                    state.session.events.size() - std::min( next, state.session.events.size() ) ) );
                for( uint32_t i = 0; i < batch.count; ++i ) batch.events[i] = tracyx::ToWire( state.session.events[next + i] );
                batch.backlog = state.session.events.size() - next - batch.count;
            }
            DWORD written = 0;
            if( !WriteFile( pipe, &batch, sizeof( batch ), &written, nullptr ) || written != sizeof( batch ) ) break;
            next += batch.count;
            if( batch.count == 0 ) std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
        }
        DisconnectNamedPipe( pipe );
        CloseHandle( pipe );
    }
}

} // namespace

int wmain( int argc, wchar_t** argv )
{
    if( argc != 3 && argc != 4 )
    {
        std::fwprintf( stderr, L"Usage: TracyXAgent <target-pid> <session-directory> [max-events]\n" );
        return 2;
    }
    wchar_t* end = nullptr;
    const auto pid = std::wcstoul( argv[1], &end, 10 );
    if( !end || *end != L'\0' || pid == 0 || pid > UINT32_MAX ) return 2;
    const auto started = ProcessStartTime( static_cast<uint32_t>( pid ) );
    if( started == 0 )
    {
        std::fwprintf( stderr, L"Cannot query target process start time.\n" );
        return 2;
    }
    LARGE_INTEGER frequency{}, origin{};
    if( !QueryPerformanceFrequency( &frequency ) || !QueryPerformanceCounter( &origin ) ) return 2;
    size_t maxEvents = 1'000'000;
    if( argc == 4 )
    {
        wchar_t* limitEnd = nullptr;
        const auto parsed = std::wcstoull( argv[3], &limitEnd, 10 );
        if( !limitEnd || *limitEnd != L'\0' || parsed < 1 || parsed > 1'000'000 ) return 2;
        maxEvents = static_cast<size_t>( parsed );
    }
    AgentState state( { static_cast<uint32_t>( pid ), started }, frequency.QuadPart, origin.QuadPart, maxEvents );
    activeAgent = &state;
    SetConsoleCtrlHandler( OnConsoleSignal, TRUE );
    const auto sessionDirectory = std::filesystem::path( argv[2] );
    const auto anchorFile = sessionDirectory / "frame-anchors.jsonl";
    size_t nextAnchor = 0;

    GpuCallbacks gpu( state );
    PsoCallbacks pso( state );
    EngineActivityCallbacks engineActivity( state );
    NoOpDxgkObjectCallbacks dxgk;
    DiagnosticCallbacks diagnostics( state );
    DxTimingCaptureEventCallbacks callbacks{};
    callbacks.GpuTimingsCallbacks = &gpu;
    callbacks.DxgkObjectCallbacks = &dxgk;
    callbacks.PipelineStateEventCallbacks = &pso;
    callbacks.GpuEngineActivityCallbacks = &engineActivity;
    callbacks.DiagnosticsSink = &diagnostics;
    DxTimingCaptureLibraryOptions options{};
    options.TrackGpuTiming = true;
    options.TrackApiObjects = true;
    options.TrackGpuEngineActivity = true;
    state.handler = DxTimingCaptureEventHandler::Create( static_cast<uint32_t>( pid ), options, callbacks );

    const auto name = std::wstring( L"TracyX." ) + std::to_wstring( GetCurrentProcessId() );
    TraceProperties properties;
    TRACEHANDLE session = 0;
    auto status = StartTraceW( &session, name.c_str(), &properties.properties );
    if( status != ERROR_SUCCESS )
    {
        std::fwprintf( stderr, L"StartTraceW failed: %lu\n", status );
        return 1;
    }

    const auto d3dKeywords = D3D12_ETW_LOG_FLAGS_RENDER_OPERATION_MARKERS |
        D3D12_ETW_LOG_FLAGS_DRIVER_CUSTOM_MARKERS | D3D12_ETW_LOG_FLAGS_BUNDLE_MARKERS |
        D3D12_ETW_LOG_FLAGS_NAMES | D3D12_ETW_LOG_FLAGS_DEVICES |
        D3D12_ETW_LOG_FLAGS_OBJECT_LIFETIME | D3D12_ETW_LOG_FLAGS_JOURNAL_ENTRIES |
        D3D12_ETW_LOG_FLAGS_RESOURCES | D3D12_ETW_LOG_APIS;
    status = EnableProvider( session, Direct3D12EtwProviderGuid, d3dKeywords, TRACE_LEVEL_RESERVED6 );
    if( status == ERROR_SUCCESS )
    {
        const auto dxgkKeywords = DXGK_KEYWORD_LOG_FLAGS_LONG_HAUL | DXGK_KEYWORD_LOG_FLAGS_BASE |
            DXGK_KEYWORD_LOG_FLAGS_PROFILER | DXGK_KEYWORD_LOG_FLAGS_HISTORY_BUFFER |
            DXGK_KEYWORD_LOG_FLAGS_RESOURCE | DXGK_KEYWORD_LOG_FLAGS_ALLOCATIONS_REFERENCES;
        status = EnableProvider( session, DxgkControlGuid, dxgkKeywords, TRACE_LEVEL_VERBOSE );
    }
    if( status != ERROR_SUCCESS )
    {
        std::fwprintf( stderr, L"EnableTraceEx2 failed: %lu\n", status );
        ControlTraceW( session, name.c_str(), &properties.properties, EVENT_TRACE_CONTROL_STOP );
        return 1;
    }

    std::thread stream( [&]() { StreamBatches( state ); } );
    std::thread bridge( [&]() {
        while( state.running.load() )
        {
            ReadAnchors( state, anchorFile, nextAnchor );
            std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
        }
    } );

#ifdef TRACYX_HAS_PRESENTMON
    std::thread presentMon( [&]() {
        tracyx::PresentMonAdapter adapter;
        std::string error;
        if( !adapter.Start( static_cast<uint32_t>( pid ), error ) )
        {
            { std::lock_guard lock( state.eventMutex ); state.presentMonError = error; }
            state.presentMonState.store( 2 );
            return;
        }
        state.presentMonState.store( 1 );
        const auto consume = [&]() {
            std::vector<tracyx::PresentMonFrame> frames;
            if( !adapter.Poll( frames, error ) ) return false;
            for( const auto& frame : frames )
            {
                tracyx::Event event;
                event.source = tracyx::Source::PresentMon;
                event.kind = tracyx::EventKind::Present;
                event.clock = tracyx::ClockDomain::WindowsQpc;
                event.rawTimestamp = frame.presentQpc;
                event.process = state.session.manifest.process;
                event.swapChainId = frame.swapChainId;
                event.displayStatus = frame.dropped ? tracyx::DisplayStatus::Dropped
                    : frame.untilDisplayedNs ? tracyx::DisplayStatus::Displayed : tracyx::DisplayStatus::Unknown;
                event.displayIntervalNs = frame.displayIntervalNs;
                event.gpuBusyNs = frame.gpuBusyNs;
                event.displayLatencyNs = frame.displayLatencyNs;
                if( event.displayStatus == tracyx::DisplayStatus::Displayed )
                {
                    const auto ticks = static_cast<long double>( *frame.untilDisplayedNs ) * frequency.QuadPart / 1'000'000'000.0L;
                    if( ticks <= static_cast<long double>( std::numeric_limits<uint64_t>::max() - frame.presentQpc ) )
                        event.rawEndTimestamp = frame.presentQpc + static_cast<uint64_t>( ticks + 0.5L );
                }
                event.label = event.displayStatus == tracyx::DisplayStatus::Dropped ? "Present dropped"
                    : event.displayStatus == tracyx::DisplayStatus::Displayed ? "Present displayed" : "Present outcome unknown";
                state.Push( std::move( event ) );
                state.presentMonFrames.fetch_add( 1 );
                if( frame.dropped ) state.presentMonDropped.fetch_add( 1 );
            }
            return true;
        };
        while( state.running.load() )
        {
            if( !consume() )
            {
                { std::lock_guard lock( state.eventMutex ); state.presentMonError = error; }
                state.presentMonState.store( 2 );
                return;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }
        for( int drain = 0; drain < 10; ++drain )
        {
            if( !consume() )
            {
                { std::lock_guard lock( state.eventMutex ); state.presentMonError = error; }
                state.presentMonState.store( 2 );
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }
    } );
#else
    { std::lock_guard lock( state.eventMutex ); state.presentMonError = "PresentMon SDK was not configured"; }
#endif

    std::thread consumer( [&]() {
        EVENT_TRACE_LOGFILEW trace{};
        trace.LoggerName = const_cast<LPWSTR>( name.c_str() );
        trace.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME |
            PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
        trace.EventRecordCallback = OnEvent;
        trace.BufferCallback = OnBuffer;
        auto handle = OpenTraceW( &trace );
        if( handle == INVALID_PROCESSTRACE_HANDLE )
        {
            std::fwprintf( stderr, L"OpenTraceW failed: %lu\n", GetLastError() );
            state.running.store( false );
            return;
        }
        const auto traceStatus = ProcessTrace( &handle, 1, nullptr, nullptr );
        if( traceStatus != ERROR_SUCCESS ) std::fwprintf( stderr, L"ProcessTrace failed: %lu\n", traceStatus );
        state.handler->OnDataComplete();
        state.handler->ReportTraceStatistics( trace );
        CloseTrace( handle );
    } );

    const auto target = OpenProcess( SYNCHRONIZE, FALSE, static_cast<DWORD>( pid ) );
    std::wprintf( L"Tracing PID %lu with RAW QPC timestamps. Press Ctrl+C to stop.\n", pid );
    while( state.running.load() )
    {
        if( target && WaitForSingleObject( target, 100 ) == WAIT_OBJECT_0 ) break;
        if( !target ) std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }
    if( target ) CloseHandle( target );
    state.running.store( false );
    ControlTraceW( session, name.c_str(), &properties.properties, EVENT_TRACE_CONTROL_STOP );
    consumer.join();
    bridge.join();
#ifdef TRACYX_HAS_PRESENTMON
    presentMon.join();
#endif
    ReadAnchors( state, anchorFile, nextAnchor );
    CancelSynchronousIo( stream.native_handle() );
    stream.join();
    state.session.manifest.lostEvents = state.lostEvents.load();
    state.session.manifest.queueDrops = state.queueDrops.load();
    state.session.manifest.bridgeMissing = state.bridgeMissing.load();
    state.session.manifest.d3d12Events = state.d3d12Events.load();
    state.session.manifest.dxgkEvents = state.dxgkEvents.load();
    state.session.manifest.d3d12DecodeErrors = state.d3d12DecodeErrors.load();
    state.session.manifest.dxgkDecodeErrors = state.dxgkDecodeErrors.load();
    state.session.manifest.presentMonState = state.presentMonState.load();
    state.session.manifest.presentMonFrames = state.presentMonFrames.load();
    state.session.manifest.presentMonDropped = state.presentMonDropped.load();
    state.session.manifest.presentMonError = state.presentMonError;
    if( std::filesystem::exists( std::filesystem::path( argv[2] ) / "tracy.tracy" ) )
    {
        state.session.manifest.tracyFile = "tracy.tracy";
    }
    std::string error;
    if( !tracyx::SaveSession( std::filesystem::path( argv[2] ), state.session, error ) )
    {
        std::fprintf( stderr, "SaveSession failed: %s\n", error.c_str() );
        return 1;
    }
    std::printf( "Saved %zu events, %llu trace losses, %llu queue drops.\n",
        state.session.events.size(), static_cast<unsigned long long>( state.session.manifest.lostEvents ),
        static_cast<unsigned long long>( state.session.manifest.queueDrops ) );
}
