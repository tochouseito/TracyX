#include "tracyx/Session.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>

namespace tracyx
{
namespace
{

bool SessionFileName( const std::string& name )
{
    return !name.empty() && name != "." && name != ".." &&
        name.find_first_of( "/\\" ) == std::string::npos &&
        name.find( ':' ) == std::string::npos &&
        std::all_of( name.begin(), name.end(), []( unsigned char ch ) { return ch >= 0x20 && ch != 0x7f; } );
}

bool SessionRegularFile( const std::filesystem::path& directory, const std::string& name )
{
    if( !SessionFileName( name ) ) return false;
    std::error_code ec;
    const auto status = std::filesystem::symlink_status( directory / name, ec );
    return !ec && std::filesystem::is_regular_file( status );
}

std::string GenerationName()
{
    static std::atomic<uint64_t> next{ 0 };
    return std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() ) +
        "-" + std::to_string( next.fetch_add( 1 ) );
}

std::string EscapeJson( std::string_view value )
{
    std::string result;
    result.reserve( value.size() + 2 );
    result.push_back( '"' );
    for( const auto ch : value )
    {
        switch( ch )
        {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if( static_cast<unsigned char>( ch ) < 0x20 )
            {
                constexpr char digits[] = "0123456789abcdef";
                result += "\\u00";
                result.push_back( digits[( static_cast<unsigned char>( ch ) >> 4 ) & 15] );
                result.push_back( digits[static_cast<unsigned char>( ch ) & 15] );
            }
            else result.push_back( ch );
        }
    }
    result.push_back( '"' );
    return result;
}

std::optional<std::string_view> Field( std::string_view line, std::string_view key )
{
    for( size_t i = 0; i < line.size(); ++i )
    {
        if( line[i] != '"' ) continue;
        auto end = i + 1;
        while( end < line.size() )
        {
            if( line[end] == '\\' ) { end += 2; continue; }
            if( line[end] == '"' ) break;
            ++end;
        }
        if( end >= line.size() ) return std::nullopt;
        if( end + 1 < line.size() && line[end + 1] == ':' && line.substr( i + 1, end - i - 1 ) == key )
            return line.substr( end + 2 );
        i = end;
    }
    return std::nullopt;
}

bool ReadNumber( std::string_view line, std::string_view key, uint64_t& value )
{
    const auto field = Field( line, key );
    if( !field ) return false;
    const auto [end, ec] = std::from_chars( field->data(), field->data() + field->size(), value );
    return ec == std::errc{} && end != field->data() &&
        ( end == field->data() + field->size() || *end == ',' || *end == '}' );
}

bool ReadString( std::string_view line, std::string_view key, std::string& value )
{
    const auto field = Field( line, key );
    if( !field || field->empty() || field->front() != '"' ) return false;
    value.clear();
    for( size_t i = 1; i < field->size(); ++i )
    {
        const auto ch = ( *field )[i];
        if( ch == '"' ) return i + 1 == field->size() || ( *field )[i + 1] == ',' || ( *field )[i + 1] == '}';
        if( ch != '\\' )
        {
            if( static_cast<unsigned char>( ch ) < 0x20 ) return false;
            value.push_back( ch );
            continue;
        }
        if( ++i == field->size() ) return false;
        switch( ( *field )[i] )
        {
        case '"': value.push_back( '"' ); break;
        case '\\': value.push_back( '\\' ); break;
        case 'n': value.push_back( '\n' ); break;
        case 'r': value.push_back( '\r' ); break;
        case 't': value.push_back( '\t' ); break;
        case 'u':
            if( i + 4 >= field->size() || ( *field )[i + 1] != '0' || ( *field )[i + 2] != '0' ) return false;
            {
                auto hex = []( char digit ) -> int {
                    if( digit >= '0' && digit <= '9' ) return digit - '0';
                    if( digit >= 'a' && digit <= 'f' ) return digit - 'a' + 10;
                    if( digit >= 'A' && digit <= 'F' ) return digit - 'A' + 10;
                    return -1;
                };
                const auto high = hex( ( *field )[i + 3] );
                const auto low = hex( ( *field )[i + 4] );
                if( high < 0 || low < 0 ) return false;
                value.push_back( static_cast<char>( high * 16 + low ) );
                i += 4;
            }
            break;
        default: return false;
        }
    }
    return false;
}

bool ReadOptionalNumber( std::string_view line, std::string_view flagKey,
    std::string_view valueKey, std::optional<uint64_t>& value )
{
    if( !Field( line, flagKey ) && !Field( line, valueKey ) ) return true;
    uint64_t hasValue = 0, parsed = 0;
    if( !ReadNumber( line, flagKey, hasValue ) || hasValue > 1 ||
        !ReadNumber( line, valueKey, parsed ) ) return false;
    if( hasValue ) value = parsed;
    return true;
}

std::string EncodeLinks( const std::vector<FrameLink>& links )
{
    std::string text;
    for( const auto& link : links )
    {
        if( !text.empty() ) text.push_back( ',' );
        text += std::to_string( link.frameId );
        text.push_back( ':' );
        text.push_back( link.strength == LinkStrength::Explicit ? 'E' : 'T' );
        text.push_back( ':' );
        text += std::to_string( link.seriesId );
    }
    return text;
}

bool DecodeLinks( std::string_view text, std::vector<FrameLink>& links )
{
    links.clear();
    while( !text.empty() )
    {
        const auto comma = text.find( ',' );
        const auto token = text.substr( 0, comma );
        const auto colon = token.find( ':' );
        if( colon == std::string_view::npos ) return false;
        uint64_t frameId = 0;
        const auto [end, ec] = std::from_chars( token.data(), token.data() + colon, frameId );
        if( ec != std::errc{} || end != token.data() + colon || frameId == 0 || colon + 1 >= token.size() ) return false;
        const auto strength = token[colon + 1];
        if( strength != 'E' && strength != 'T' ) return false;
        uint64_t seriesId = 0;
        if( colon + 2 != token.size() )
        {
            if( token[colon + 2] != ':' ) return false;
            const auto first = token.data() + colon + 3;
            const auto [seriesEnd, seriesError] = std::from_chars( first, token.data() + token.size(), seriesId );
            if( seriesError != std::errc{} || seriesEnd != token.data() + token.size() ) return false;
        }
        links.push_back( { frameId, strength == 'E' ? LinkStrength::Explicit : LinkStrength::TimeOverlap, seriesId } );
        if( comma == std::string_view::npos ) break;
        text.remove_prefix( comma + 1 );
    }
    return true;
}

bool ReadEvent( std::string_view line, Event& event )
{
    uint64_t version, source, kind, clock, processId, threadId, normalized, hasNormalized;
    uint64_t rawEnd, hasRawEnd, normalizedEnd, hasNormalizedEnd;
    if( !ReadNumber( line, "version", version ) || version != SessionVersion ||
        !ReadNumber( line, "event_id", event.eventId ) ||
        !ReadNumber( line, "source", source ) || source > static_cast<uint64_t>( Source::Pix ) ||
        !ReadNumber( line, "kind", kind ) || kind > static_cast<uint64_t>( EventKind::ResourceDestroy ) ||
        !ReadNumber( line, "clock", clock ) || clock > static_cast<uint64_t>( ClockDomain::PresentMon ) ||
        !ReadNumber( line, "raw_timestamp", event.rawTimestamp ) ||
        !ReadNumber( line, "has_raw_end", hasRawEnd ) || hasRawEnd > 1 ||
        !ReadNumber( line, "raw_end_timestamp", rawEnd ) ||
        !ReadNumber( line, "has_normalized", hasNormalized ) || hasNormalized > 1 ||
        !ReadNumber( line, "normalized_ns", normalized ) ||
        !ReadNumber( line, "has_normalized_end", hasNormalizedEnd ) || hasNormalizedEnd > 1 ||
        !ReadNumber( line, "normalized_end_ns", normalizedEnd ) ||
        !ReadNumber( line, "uncertainty_ns", event.uncertaintyNs ) ||
        !ReadNumber( line, "process_id", processId ) || processId > std::numeric_limits<uint32_t>::max() ||
        !ReadNumber( line, "process_start_100ns", event.process.startTime100ns ) ||
        !ReadNumber( line, "thread_id", threadId ) || threadId > std::numeric_limits<uint32_t>::max() ||
        !ReadNumber( line, "device_id", event.deviceId ) ||
        !ReadNumber( line, "queue_id", event.queueId ) ||
        !ReadNumber( line, "command_list_id", event.commandListId ) ||
        !ReadNumber( line, "resource_id", event.resourceId ) ||
        !ReadString( line, "label", event.label ) ) return false;

    std::string links;
    if( !ReadString( line, "frame_links", links ) || !DecodeLinks( links, event.frameLinks ) ) return false;
    uint64_t displayStatus = 0;
    if( Field( line, "swap_chain_id" ) && !ReadNumber( line, "swap_chain_id", event.swapChainId ) ) return false;
    if( Field( line, "display_status" ) &&
        ( !ReadNumber( line, "display_status", displayStatus ) ||
          displayStatus > static_cast<uint64_t>( DisplayStatus::Dropped ) ) ) return false;
    if( !ReadOptionalNumber( line, "has_display_interval", "display_interval_ns", event.displayIntervalNs ) ||
        !ReadOptionalNumber( line, "has_gpu_busy", "gpu_busy_ns", event.gpuBusyNs ) ||
        !ReadOptionalNumber( line, "has_display_latency", "display_latency_ns", event.displayLatencyNs ) ) return false;
    event.displayStatus = static_cast<DisplayStatus>( displayStatus );
    event.source = static_cast<Source>( source );
    event.kind = static_cast<EventKind>( kind );
    event.clock = static_cast<ClockDomain>( clock );
    event.process.processId = static_cast<uint32_t>( processId );
    event.threadId = static_cast<uint32_t>( threadId );
    event.normalizedNs = hasNormalized != 0 ? std::optional<uint64_t>( normalized ) : std::nullopt;
    event.rawEndTimestamp = hasRawEnd != 0 ? std::optional<uint64_t>( rawEnd ) : std::nullopt;
    event.normalizedEndNs = hasNormalizedEnd != 0 ? std::optional<uint64_t>( normalizedEnd ) : std::nullopt;
    return true;
}

} // namespace

bool SaveSession( const std::filesystem::path& directory, const Session& session, std::string& error )
{
    error.clear();
    if( session.manifest.version != SessionVersion )
    {
        error = "Unsupported session version";
        return false;
    }
    if( !session.manifest.tracyFile.empty() && !SessionFileName( session.manifest.tracyFile ) )
    {
        error = "Tracy capture must be a file in the session directory";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories( directory, ec );
    if( ec ) { error = ec.message(); return false; }

    const auto manifestPath = directory / "session.json";
    const auto generation = GenerationName();
    const auto eventsFile = "events-" + generation + ".jsonl";
    const auto eventsPath = directory / eventsFile;
    const auto temporaryManifest = directory / ( "session-" + generation + ".tmp" );
    if( std::filesystem::exists( eventsPath, ec ) || std::filesystem::exists( temporaryManifest, ec ) || ec )
    {
        error = "Session generation collision or path check failed";
        return false;
    }
    std::ofstream manifest( temporaryManifest, std::ios::binary | std::ios::trunc );
    std::ofstream events( eventsPath, std::ios::binary | std::ios::trunc );
    if( !manifest || !events )
    {
        error = "Cannot open session files";
        manifest.close();
        events.close();
        std::filesystem::remove( temporaryManifest, ec );
        std::filesystem::remove( eventsPath, ec );
        return false;
    }

    manifest << "{\"version\":" << SessionVersion
        << ",\"process_id\":" << session.manifest.process.processId
        << ",\"process_start_100ns\":" << session.manifest.process.startTime100ns
        << ",\"qpc_frequency\":" << session.manifest.qpcFrequency
        << ",\"qpc_origin\":" << session.manifest.qpcOrigin
        << ",\"lost_events\":" << session.manifest.lostEvents
        << ",\"queue_drops\":" << session.manifest.queueDrops
        << ",\"bridge_missing\":" << session.manifest.bridgeMissing
        << ",\"d3d12_events\":" << session.manifest.d3d12Events
        << ",\"dxgk_events\":" << session.manifest.dxgkEvents
        << ",\"d3d12_decode_errors\":" << session.manifest.d3d12DecodeErrors
        << ",\"dxgk_decode_errors\":" << session.manifest.dxgkDecodeErrors
        << ",\"clock_residual_ns\":" << session.manifest.clockResidualNs
        << ",\"present_mon_state\":" << session.manifest.presentMonState
        << ",\"present_mon_frames\":" << session.manifest.presentMonFrames
        << ",\"present_mon_dropped\":" << session.manifest.presentMonDropped
        << ",\"present_mon_error\":" << EscapeJson( session.manifest.presentMonError )
        << ",\"build_sha\":" << EscapeJson( session.manifest.buildSha )
        << ",\"environment\":" << EscapeJson( session.manifest.environment )
        << ",\"gpu_name\":" << EscapeJson( session.manifest.gpuName )
        << ",\"driver_version\":" << EscapeJson( session.manifest.driverVersion )
        << ",\"scene\":" << EscapeJson( session.manifest.scene )
        << ",\"capture_mode\":" << EscapeJson( session.manifest.captureMode )
        << ",\"event_count\":" << session.events.size()
        << ",\"events_file\":" << EscapeJson( eventsFile )
        << ",\"tracy_file\":" << EscapeJson( session.manifest.tracyFile ) << "}\n";

    for( const auto& event : session.events )
    {
        events << "{\"version\":" << SessionVersion
            << ",\"event_id\":" << event.eventId
            << ",\"source\":" << static_cast<unsigned>( event.source )
            << ",\"kind\":" << static_cast<unsigned>( event.kind )
            << ",\"clock\":" << static_cast<unsigned>( event.clock )
            << ",\"raw_timestamp\":" << event.rawTimestamp
            << ",\"has_raw_end\":" << ( event.rawEndTimestamp ? 1 : 0 )
            << ",\"raw_end_timestamp\":" << event.rawEndTimestamp.value_or( 0 )
            << ",\"has_normalized\":" << ( event.normalizedNs ? 1 : 0 )
            << ",\"normalized_ns\":" << event.normalizedNs.value_or( 0 )
            << ",\"has_normalized_end\":" << ( event.normalizedEndNs ? 1 : 0 )
            << ",\"normalized_end_ns\":" << event.normalizedEndNs.value_or( 0 )
            << ",\"uncertainty_ns\":" << event.uncertaintyNs
            << ",\"process_id\":" << event.process.processId
            << ",\"process_start_100ns\":" << event.process.startTime100ns
            << ",\"thread_id\":" << event.threadId
            << ",\"device_id\":" << event.deviceId
            << ",\"queue_id\":" << event.queueId
            << ",\"command_list_id\":" << event.commandListId
            << ",\"resource_id\":" << event.resourceId;
        if( event.source == Source::PresentMon )
            events << ",\"swap_chain_id\":" << event.swapChainId
                << ",\"display_status\":" << static_cast<unsigned>( event.displayStatus )
                << ",\"has_display_interval\":" << ( event.displayIntervalNs ? 1 : 0 )
                << ",\"display_interval_ns\":" << event.displayIntervalNs.value_or( 0 )
                << ",\"has_gpu_busy\":" << ( event.gpuBusyNs ? 1 : 0 )
                << ",\"gpu_busy_ns\":" << event.gpuBusyNs.value_or( 0 )
                << ",\"has_display_latency\":" << ( event.displayLatencyNs ? 1 : 0 )
                << ",\"display_latency_ns\":" << event.displayLatencyNs.value_or( 0 );
        events << ",\"label\":" << EscapeJson( event.label )
            << ",\"frame_links\":" << EscapeJson( EncodeLinks( event.frameLinks ) ) << "}\n";
    }
    manifest.close();
    events.close();
    if( !manifest || !events )
    {
        error = "Session write failed; previous manifest remains intact";
        std::filesystem::remove( temporaryManifest, ec );
        std::filesystem::remove( eventsPath, ec );
        return false;
    }
    std::filesystem::rename( temporaryManifest, manifestPath, ec );
    if( ec )
    {
        error = "Cannot publish session manifest: " + ec.message();
        std::filesystem::remove( temporaryManifest, ec );
        std::filesystem::remove( eventsPath, ec );
        return false;
    }
    return true;
}

bool LoadSession( const std::filesystem::path& directory, Session& session, std::string& error )
{
    error.clear();
    std::ifstream manifest( directory / "session.json", std::ios::binary );
    if( !manifest ) { error = "Session manifest missing"; return false; }

    std::string line;
    if( !std::getline( manifest, line ) ) { error = "Empty manifest"; return false; }
    Session loaded;
    uint64_t version = 0, processId = 0, expectedEvents = 0;
    if( !ReadNumber( line, "version", version ) || version != SessionVersion ||
        !ReadNumber( line, "process_id", processId ) || processId > std::numeric_limits<uint32_t>::max() ||
        !ReadNumber( line, "process_start_100ns", loaded.manifest.process.startTime100ns ) ||
        !ReadNumber( line, "qpc_frequency", loaded.manifest.qpcFrequency ) ||
        !ReadNumber( line, "qpc_origin", loaded.manifest.qpcOrigin ) ||
        !ReadNumber( line, "lost_events", loaded.manifest.lostEvents ) ||
        !ReadNumber( line, "queue_drops", loaded.manifest.queueDrops ) ||
        !ReadNumber( line, "event_count", expectedEvents ) ||
        !ReadNumber( line, "clock_residual_ns", loaded.manifest.clockResidualNs ) ||
        !ReadString( line, "tracy_file", loaded.manifest.tracyFile ) )
    {
        error = "Invalid or unsupported manifest";
        return false;
    }
    loaded.manifest.process.processId = static_cast<uint32_t>( processId );
    loaded.manifest.eventsFile = "events.jsonl";
    if( Field( line, "events_file" ) && !ReadString( line, "events_file", loaded.manifest.eventsFile ) )
    {
        error = "Invalid event file name";
        return false;
    }
    ReadNumber( line, "bridge_missing", loaded.manifest.bridgeMissing );
    ReadNumber( line, "d3d12_events", loaded.manifest.d3d12Events );
    ReadNumber( line, "dxgk_events", loaded.manifest.dxgkEvents );
    ReadNumber( line, "d3d12_decode_errors", loaded.manifest.d3d12DecodeErrors );
    ReadNumber( line, "dxgk_decode_errors", loaded.manifest.dxgkDecodeErrors );
    uint64_t presentMonState = 0;
    if( Field( line, "present_mon_state" ) &&
        ( !ReadNumber( line, "present_mon_state", presentMonState ) || presentMonState > 2 ) )
    {
        error = "Invalid PresentMon state";
        return false;
    }
    loaded.manifest.presentMonState = static_cast<uint32_t>( presentMonState );
    if( Field( line, "present_mon_frames" ) &&
        !ReadNumber( line, "present_mon_frames", loaded.manifest.presentMonFrames ) ) return false;
    if( Field( line, "present_mon_dropped" ) &&
        !ReadNumber( line, "present_mon_dropped", loaded.manifest.presentMonDropped ) ) return false;
    if( Field( line, "present_mon_error" ) &&
        !ReadString( line, "present_mon_error", loaded.manifest.presentMonError ) ) return false;
    const auto readCondition = [&]( const char* key, std::string& value ) {
        return !Field( line, key ) || ReadString( line, key, value );
    };
    if( !readCondition( "build_sha", loaded.manifest.buildSha ) ||
        !readCondition( "environment", loaded.manifest.environment ) ||
        !readCondition( "gpu_name", loaded.manifest.gpuName ) ||
        !readCondition( "driver_version", loaded.manifest.driverVersion ) ||
        !readCondition( "scene", loaded.manifest.scene ) ||
        !readCondition( "capture_mode", loaded.manifest.captureMode ) )
    {
        error = "Invalid comparison conditions";
        return false;
    }
    if( !loaded.manifest.tracyFile.empty() &&
        !SessionRegularFile( directory, loaded.manifest.tracyFile ) )
    {
        error = "Referenced Tracy capture is missing or outside the session";
        return false;
    }
    if( !SessionFileName( loaded.manifest.eventsFile ) )
    {
        error = "Event file is outside the session";
        return false;
    }
    if( !SessionRegularFile( directory, loaded.manifest.eventsFile ) )
    {
        error = "Event file missing or not a regular file";
        return false;
    }
    std::ifstream events( directory / loaded.manifest.eventsFile, std::ios::binary );
    if( !events ) { error = "Event file missing"; return false; }
    for( size_t lineNumber = 1; std::getline( events, line ); ++lineNumber )
    {
        Event event;
        if( !ReadEvent( line, event ) )
        {
            error = "Invalid event at line " + std::to_string( lineNumber );
            return false;
        }
        loaded.events.push_back( std::move( event ) );
    }
    if( events.bad() ) { error = "Event read failed"; return false; }
    if( loaded.events.size() != expectedEvents )
    {
        error = "Event count mismatch";
        return false;
    }
    session = std::move( loaded );
    return true;
}

bool LoadFrameAnchors( const std::filesystem::path& file, std::vector<FrameAnchorRecord>& anchors, std::string& error )
{
    error.clear();
    std::ifstream input( file, std::ios::binary );
    if( !input ) { error = "Frame anchor file missing"; return false; }
    std::vector<FrameAnchorRecord> loaded;
    std::string line;
    for( size_t lineNumber = 1; std::getline( input, line ); ++lineNumber )
    {
        FrameAnchorRecord anchor;
        if( !ReadNumber( line, "frame_id", anchor.frameId ) || anchor.frameId == 0 ||
            !ReadNumber( line, "qpc_before", anchor.qpcBefore ) ||
            !ReadNumber( line, "qpc_after", anchor.qpcAfter ) || anchor.qpcAfter < anchor.qpcBefore )
        {
            error = "Invalid frame anchor at line " + std::to_string( lineNumber );
            return false;
        }
        ReadNumber( line, "series_id", anchor.seriesId );
        loaded.push_back( anchor );
    }
    if( input.bad() ) { error = "Frame anchor read failed"; return false; }
    anchors = std::move( loaded );
    return true;
}

bool AppendPixCapture( const std::filesystem::path& file, const PixCaptureRecord& capture, std::string& error )
{
    error.clear();
    if( !SessionFileName( capture.fileName ) || capture.qpcEnd < capture.qpcBegin )
    {
        error = "Invalid PIX capture record";
        return false;
    }
    std::ofstream output( file, std::ios::binary | std::ios::app );
    if( !output ) { error = "Cannot open PIX capture index"; return false; }
    output << "{\"version\":1,\"kind\":" << static_cast<unsigned>( capture.kind )
        << ",\"frame_id\":" << capture.frameId
        << ",\"series_id\":" << capture.seriesId
        << ",\"qpc_begin\":" << capture.qpcBegin
        << ",\"qpc_end\":" << capture.qpcEnd
        << ",\"begin_result\":" << capture.beginResult
        << ",\"end_result\":" << capture.endResult
        << ",\"started\":" << ( capture.started ? 1 : 0 )
        << ",\"completed\":" << ( capture.completed ? 1 : 0 )
        << ",\"file_name\":" << EscapeJson( capture.fileName )
        << ",\"error\":" << EscapeJson( capture.error ) << "}\n";
    output.flush();
    if( !output ) { error = "PIX capture index write failed"; return false; }
    return true;
}

bool LoadPixCaptures( const std::filesystem::path& file, std::vector<PixCaptureRecord>& captures, std::string& error )
{
    error.clear();
    std::ifstream input( file, std::ios::binary );
    if( !input ) { error = "PIX capture index missing"; return false; }
    std::vector<PixCaptureRecord> loaded;
    std::string line;
    for( size_t lineNumber = 1; std::getline( input, line ); ++lineNumber )
    {
        PixCaptureRecord capture;
        uint64_t version = 0, kind = 0, beginResult = 0, endResult = 0, started = 0, completed = 0;
        if( !ReadNumber( line, "version", version ) || version != 1 ||
            !ReadNumber( line, "kind", kind ) || kind > static_cast<uint64_t>( PixCaptureKind::Existing ) ||
            !ReadNumber( line, "frame_id", capture.frameId ) ||
            !ReadNumber( line, "qpc_begin", capture.qpcBegin ) ||
            !ReadNumber( line, "qpc_end", capture.qpcEnd ) || capture.qpcEnd < capture.qpcBegin ||
            !ReadNumber( line, "begin_result", beginResult ) || beginResult > UINT32_MAX ||
            !ReadNumber( line, "end_result", endResult ) || endResult > UINT32_MAX ||
            !ReadNumber( line, "started", started ) || started > 1 ||
            !ReadNumber( line, "completed", completed ) || completed > 1 ||
            !ReadString( line, "file_name", capture.fileName ) || !SessionFileName( capture.fileName ) ||
            !ReadString( line, "error", capture.error ) )
        {
            error = "Invalid PIX capture record at line " + std::to_string( lineNumber );
            return false;
        }
        if( Field( line, "series_id" ) && !ReadNumber( line, "series_id", capture.seriesId ) )
        {
            error = "Invalid PIX capture series at line " + std::to_string( lineNumber );
            return false;
        }
        capture.kind = static_cast<PixCaptureKind>( kind );
        capture.beginResult = static_cast<uint32_t>( beginResult );
        capture.endResult = static_cast<uint32_t>( endResult );
        capture.started = started != 0;
        capture.completed = completed != 0;
        loaded.push_back( std::move( capture ) );
    }
    if( input.bad() ) { error = "PIX capture index read failed"; return false; }
    captures = std::move( loaded );
    return true;
}

std::vector<FrameLink> FindPixCaptureFrames( const PixCaptureRecord& capture,
    const std::vector<FrameAnchorRecord>& anchors )
{
    std::vector<FrameLink> links;
    if( !capture.completed || capture.kind == PixCaptureKind::Existing ) return links;
    for( const auto& anchor : anchors )
    {
        if( anchor.seriesId != capture.seriesId || anchor.frameId == 0 ) continue;
        if( anchor.frameId == capture.frameId )
            links.push_back( { anchor.frameId, LinkStrength::Explicit, anchor.seriesId } );
        else if( capture.qpcBegin != 0 && capture.qpcEnd >= capture.qpcBegin &&
            anchor.qpcBefore >= capture.qpcBegin && anchor.qpcBefore <= capture.qpcEnd )
            links.push_back( { anchor.frameId, LinkStrength::TimeOverlap, anchor.seriesId } );
    }
    return links;
}

} // namespace tracyx
