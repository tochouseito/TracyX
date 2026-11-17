#include "tracyx/Clock.hpp"
#include "tracyx/Correlation.hpp"
#include "tracyx/Diagnostics.hpp"
#include "tracyx/Comparison.hpp"
#include "tracyx/FrameBridge.hpp"
#include "tracyx/LiveProtocol.hpp"
#include "tracyx/Session.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

int main( int argc, char** argv )
{
    using namespace tracyx;

    if( argc == 2 )
    {
        Session captured;
        std::string error;
        if( !LoadSession( argv[1], captured, error ) || captured.events.empty() )
        {
            std::cerr << "Captured session invalid: " << error << '\n';
            return 1;
        }
        std::cout << "Loaded " << captured.events.size() << " captured events\n";
        return 0;
    }

    QpcClock clock( 10'000'000, 1'000 );
    assert( !clock.Normalize( 999 ) );
    assert( clock.Normalize( 10'001 )->nanoseconds == 900'100 );
    assert( !QpcClock( 0, 0 ).Normalize( 1 ) );

    TracyClockFit fit( 10'000'000 );
    assert( !fit.ToTracyNs( 1'000 ) );
    assert( fit.AddAnchor( { 990, 1'010, 100'000 } ) );
    assert( !fit.ToTracyNs( 1'000 ) );
    assert( fit.AddAnchor( { 1'990, 2'010, 200'000 } ) );
    assert( fit.ToTracyNs( 1'500 )->nanoseconds == 150'000 );
    assert( !fit.ToTracyNs( 2'500 ) );
    assert( !fit.AddAnchor( { 1'500, 1'600, 250'000 } ) );
    TracyClockFit piecewise( 10'000'000 );
    assert( piecewise.AddAnchor( { 990, 1'010, 100'000 } ) );
    assert( piecewise.AddAnchor( { 1'990, 2'010, 200'000 } ) );
    assert( piecewise.AddAnchor( { 2'990, 3'010, 320'000 } ) );
    assert( piecewise.ToTracyNs( 2'500 )->nanoseconds == 260'000 );
    assert( piecewise.MaxResidualNs() > 0 );

    FrameCorrelator frames;
    const ProcessIdentity process{ 42, 300 };
    assert( frames.AddFrame( { 1, process, 100, 200 } ) );
    assert( frames.AddFrame( { 2, process, 180, 280 } ) );
    assert( !frames.AddFrame( { 2, process, 300, 400 } ) );
    assert( frames.AddFrame( { 2, process, 300, 400, 7 } ) );
    Event event;
    event.process = process;
    assert( frames.FindLinks( event ).empty() );
    event.process = process;
    event.source = Source::PresentMon;
    event.kind = EventKind::Present;
    event.normalizedNs = 150;
    event.normalizedEndNs = 220;
    assert( frames.FindLinks( event ).size() == 2 );
    event.normalizedEndNs.reset();
    event.normalizedNs.reset();
    event.normalizedNs = 190;
    const auto links = frames.FindLinks( event );
    assert( links.size() == 2 );
    assert( links[0].strength == LinkStrength::TimeOverlap );
    event.frameLinks = { { 2, LinkStrength::Explicit } };
    assert( frames.FindLinks( event )[0].strength == LinkStrength::Explicit );
    event.frameLinks.clear();
    event.process.startTime100ns++;
    assert( frames.FindLinks( event ).empty() );

    Session diagnosticSession;
    diagnosticSession.manifest.process = process;
    const auto diagnosticEvent = [&]( uint64_t id, EventKind kind, uint64_t begin, uint64_t end, uint64_t command ) {
        Event item;
        item.eventId = id;
        item.kind = kind;
        item.process = process;
        item.normalizedNs = begin;
        item.normalizedEndNs = end;
        item.commandListId = command;
        return item;
    };
    auto firstFrame = diagnosticEvent( 1, EventKind::FrameAnchor, 100, 101, 0 );
    firstFrame.frameLinks = { { 1, LinkStrength::Explicit, 0 } };
    auto nextFrame = diagnosticEvent( 2, EventKind::FrameAnchor, 200, 201, 0 );
    nextFrame.frameLinks = { { 2, LinkStrength::Explicit, 0 } };
    diagnosticSession.events = { firstFrame, nextFrame,
        diagnosticEvent( 3, EventKind::PsoCompile, 120, 145, 0 ),
        diagnosticEvent( 4, EventKind::Residency, 130, 140, 0 ),
        diagnosticEvent( 5, EventKind::QueueSubmit, 110, 110, 7 ),
        diagnosticEvent( 6, EventKind::GpuWork, 180, 190, 7 << 16 ) };
    const auto diagnosis = AnalyzeFrameDiagnostics( diagnosticSession, 0, 1 );
    assert( diagnosis.hasFrameWindow && diagnosis.candidates.size() == 3 );
    assert( diagnosis.candidates[0].kind == DiagnosticKind::QueueWait );
    assert( diagnosis.candidates[0].evidenceEventId == 5 && diagnosis.candidates[0].relatedEventId == 6 );
    assert( diagnosis.candidates[0].overlapNs == 70 );
    diagnosticSession.manifest.queueDrops = 1;
    assert( AnalyzeFrameDiagnostics( diagnosticSession, 0, 1 ).degradedQuality );
    diagnosticSession.events.erase( diagnosticSession.events.begin() + 1 );
    assert( !AnalyzeFrameDiagnostics( diagnosticSession, 0, 1 ).hasFrameWindow );

    Session before, after;
    before.manifest.process = process;
    after.manifest.process = process;
    before.manifest.buildSha = "before";
    after.manifest.buildSha = "after";
    before.manifest.environment = after.manifest.environment = "Windows test";
    before.manifest.gpuName = after.manifest.gpuName = "GPU A";
    before.manifest.driverVersion = after.manifest.driverVersion = "1.0";
    before.manifest.scene = after.manifest.scene = "repeatable scene";
    before.manifest.captureMode = after.manifest.captureMode = "System";
    for( uint64_t frame = 1; frame <= 101; ++frame )
    {
        auto anchor = diagnosticEvent( frame, EventKind::FrameAnchor, frame * 1'000'000, frame * 1'000'000, 0 );
        anchor.frameLinks = { { frame, LinkStrength::Explicit, 0 } };
        before.events.push_back( anchor );
        anchor.normalizedNs = frame * 2'000'000;
        after.events.push_back( anchor );
        if( frame <= 100 )
        {
            auto present = diagnosticEvent( 200 + frame, EventKind::Present, 0, 0, 0 );
            present.source = Source::PresentMon;
            present.displayStatus = DisplayStatus::Displayed;
            present.displayIntervalNs = frame * 1'000'000;
            before.events.push_back( present );
            present.displayIntervalNs = frame * 2'000'000;
            after.events.push_back( present );
            auto gpu = diagnosticEvent( 400 + frame, EventKind::GpuWork, 0, frame * 1'000'000, 1 );
            before.events.push_back( gpu );
            gpu.normalizedEndNs = frame * 2'000'000;
            after.events.push_back( gpu );
        }
    }
    const auto comparison = CompareSessions( before, after );
    assert( comparison.frame.before.count == 100 && comparison.frame.after.p95Ns == 2'000'000 );
    assert( comparison.present.before.p95Ns == 95'000'000 && comparison.present.after.p99Ns == 198'000'000 );
    assert( comparison.gpuWork.p95ChangePercent == 100.0 );
    assert( comparison.warnings.empty() );
    after.manifest.captureMode = "Deep";
    after.manifest.queueDrops = 1;
    assert( CompareSessions( before, after ).warnings.size() == 2 );

    const auto directory = std::filesystem::temp_directory_path() / "tracyx-core-test";
    std::filesystem::create_directories( directory );
    Session saved;
    saved.manifest.process = process;
    saved.manifest.qpcFrequency = 10'000'000;
    saved.manifest.tracyFile = "capture.tracy";
    saved.manifest.presentMonState = 1;
    saved.manifest.presentMonFrames = 2;
    saved.manifest.presentMonDropped = 1;
    saved.manifest.buildSha = "abc123";
    saved.manifest.scene = "scene-one";
    saved.manifest.captureMode = "System";
    std::ofstream( directory / "capture.tracy", std::ios::binary ).put( 'T' );
    event.process = process;
    event.label = "Queue \\\"A\\\"\\nGPU";
    event.frameLinks = { { 1, LinkStrength::Explicit, 7 }, { 2, LinkStrength::TimeOverlap, 8 } };
    saved.events.push_back( event );
    Event displayed;
    displayed.source = Source::PresentMon;
    displayed.kind = EventKind::Present;
    displayed.process = process;
    displayed.rawTimestamp = 10'000;
    displayed.rawEndTimestamp = 10'200;
    displayed.swapChainId = 0x1234;
    displayed.displayStatus = DisplayStatus::Displayed;
    displayed.displayIntervalNs = 16'000'000;
    displayed.gpuBusyNs = 0;
    displayed.displayLatencyNs = 12'000'000;
    displayed.label = "Present displayed";
    saved.events.push_back( displayed );
    Event dropped = displayed;
    dropped.rawTimestamp = 10'300;
    dropped.rawEndTimestamp.reset();
    dropped.displayStatus = DisplayStatus::Dropped;
    dropped.displayIntervalNs.reset();
    dropped.displayLatencyNs.reset();
    dropped.label = "Present dropped";
    saved.events.push_back( dropped );
    Event storage = event;
    storage.kind = EventKind::StorageRead;
    storage.queueId = 7;
    storage.resourceId = 3;
    storage.label = "DirectStorage read";
    saved.events.push_back( storage );
    Event resource = event;
    resource.kind = EventKind::ResourceDestroy;
    resource.resourceId = 99;
    resource.label = "Resource destroy";
    saved.events.push_back( resource );
    std::string error;
    assert( SaveSession( directory, saved, error ) );
    Session loaded;
    assert( LoadSession( directory, loaded, error ) );
    assert( loaded.manifest.eventsFile.starts_with( "events-" ) );
    assert( loaded.manifest.process == process );
    assert( loaded.events.size() == 5 );
    assert( loaded.manifest.presentMonFrames == 2 );
    assert( loaded.manifest.presentMonDropped == 1 );
    assert( loaded.manifest.buildSha == "abc123" && loaded.manifest.scene == "scene-one" );
    assert( loaded.manifest.captureMode == "System" );
    assert( loaded.events[1].swapChainId == 0x1234 );
    assert( loaded.events[1].displayStatus == DisplayStatus::Displayed );
    assert( loaded.events[1].gpuBusyNs == 0 );
    assert( loaded.events[2].displayStatus == DisplayStatus::Dropped );
    assert( !loaded.events[2].rawEndTimestamp );
    assert( !loaded.events[2].displayIntervalNs );
    assert( loaded.events[3].kind == EventKind::StorageRead && loaded.events[3].queueId == 7 );
    assert( loaded.events[4].kind == EventKind::ResourceDestroy && loaded.events[4].resourceId == 99 );
    assert( loaded.events[0].label == event.label );
    assert( loaded.events[0].frameLinks.size() == 2 );
    assert( loaded.events[0].frameLinks[1].strength == LinkStrength::TimeOverlap );
    assert( loaded.events[0].frameLinks[0].seriesId == 7 );
    assert( loaded.events[0].frameLinks[1].seriesId == 8 );
    PixCaptureRecord pix;
    pix.kind = PixCaptureKind::Gpu;
    pix.frameId = 42;
    pix.qpcBegin = 1'000;
    pix.qpcEnd = 2'000;
    pix.started = true;
    pix.completed = true;
    pix.fileName = "GPU test.wpix";
    std::ofstream( directory / "pix-captures.jsonl", std::ios::trunc ).close();
    assert( AppendPixCapture( directory / "pix-captures.jsonl", pix, error ) );
    pix.kind = PixCaptureKind::Timing;
    pix.completed = false;
    pix.beginResult = 0x80070005;
    pix.error = "elevation required\nrestart";
    assert( AppendPixCapture( directory / "pix-captures.jsonl", pix, error ) );
    std::vector<PixCaptureRecord> pixLoaded;
    assert( LoadPixCaptures( directory / "pix-captures.jsonl", pixLoaded, error ) );
    assert( pixLoaded.size() == 2 );
    assert( pixLoaded[0].frameId == 42 );
    assert( pixLoaded.back().beginResult == 0x80070005 );
    assert( pixLoaded.back().error == pix.error );
    assert( pixLoaded[0].seriesId == 0 );
    auto outsideCapture = pix;
    outsideCapture.fileName = "../outside.wpix";
    assert( !AppendPixCapture( directory / "pix-captures.jsonl", outsideCapture, error ) );
    std::ifstream pixInput( directory / "pix-captures.jsonl" );
    std::string invalidPixLine;
    assert( static_cast<bool>( std::getline( pixInput, invalidPixLine ) ) );
    pixInput.close();
    const auto pixName = invalidPixLine.find( "\"file_name\":" );
    const auto pixError = invalidPixLine.find( ",\"error\":", pixName );
    assert( pixName != std::string::npos && pixError != std::string::npos );
    invalidPixLine.replace( pixName, pixError - pixName, "\"file_name\":\"../outside.wpix\"" );
    { std::ofstream output( directory / "pix-captures.jsonl", std::ios::app ); output << invalidPixLine << '\n'; }
    assert( !LoadPixCaptures( directory / "pix-captures.jsonl", pixLoaded, error ) );
    const std::vector<FrameAnchorRecord> pixAnchors{
        { 41, 900, 901, 0 }, { 42, 990, 995, 0 }, { 43, 1'500, 1'505, 0 },
        { 44, 2'100, 2'105, 0 }, { 42, 1'100, 1'105, 1 }
    };
    const auto pixLinks = FindPixCaptureFrames( pixLoaded[0], pixAnchors );
    assert( pixLinks.size() == 2 );
    assert( pixLinks[0].frameId == 42 && pixLinks[0].strength == LinkStrength::Explicit );
    assert( pixLinks[1].frameId == 43 && pixLinks[1].strength == LinkStrength::TimeOverlap );
    assert( FindPixCaptureFrames( pixLoaded[1], pixAnchors ).empty() );
    std::ofstream( directory / loaded.manifest.eventsFile, std::ios::trunc ).close();
    assert( !LoadSession( directory, loaded, error ) );
    assert( error == "Event count mismatch" );
    assert( SaveSession( directory, saved, error ) );
    assert( LoadSession( directory, loaded, error ) );
    const auto previousEventsFile = loaded.manifest.eventsFile;
    assert( SaveSession( directory, saved, error ) );
    assert( LoadSession( directory, loaded, error ) );
    assert( loaded.manifest.eventsFile != previousEventsFile );
#ifdef _WIN32
    const auto publishedEventsFile = loaded.manifest.eventsFile;
    {
        std::ifstream heldManifest( directory / "session.json" );
        assert( heldManifest );
        assert( !SaveSession( directory, saved, error ) );
        assert( error.starts_with( "Cannot publish session manifest" ) );
    }
    assert( LoadSession( directory, loaded, error ) );
    assert( loaded.manifest.eventsFile == publishedEventsFile );
#endif
    std::ofstream( directory / previousEventsFile, std::ios::trunc ).close();
    assert( LoadSession( directory, loaded, error ) );
    {
        std::ofstream manifest( directory / "session.json", std::ios::trunc );
        manifest << "{\"version\":1,\"process_id\":42,\"process_start_100ns\":300,"
            "\"qpc_frequency\":10000000,\"qpc_origin\":0,\"lost_events\":0,"
            "\"queue_drops\":0,\"clock_residual_ns\":0,\"tracy_file\":\"capture.tracy\"}\n";
    }
    assert( !LoadSession( directory, loaded, error ) );
    assert( error == "Invalid or unsupported manifest" );
    assert( SaveSession( directory, saved, error ) );
    assert( LoadSession( directory, loaded, error ) );
    std::ifstream manifestInput( directory / "session.json" );
    std::string manifestLine;
    assert( static_cast<bool>( std::getline( manifestInput, manifestLine ) ) );
    manifestInput.close();
    const auto eventsField = manifestLine.find( ",\"events_file\":" );
    const auto tracyField = manifestLine.find( ",\"tracy_file\":", eventsField );
    assert( eventsField != std::string::npos && tracyField != std::string::npos );
    auto invalidPath = manifestLine;
    invalidPath.replace( eventsField, tracyField - eventsField, ",\"events_file\":\"../outside.jsonl\"" );
    { std::ofstream output( directory / "session.json", std::ios::trunc ); output << invalidPath << '\n'; }
    assert( !LoadSession( directory, loaded, error ) && error == "Event file is outside the session" );
    std::filesystem::copy_file( directory / loaded.manifest.eventsFile, directory / "events.jsonl",
        std::filesystem::copy_options::overwrite_existing );
    auto legacy = manifestLine;
    legacy.erase( eventsField, tracyField - eventsField );
    { std::ofstream output( directory / "session.json", std::ios::trunc ); output << legacy << '\n'; }
    assert( LoadSession( directory, loaded, error ) && loaded.manifest.eventsFile == "events.jsonl" );
    assert( SaveSession( directory, saved, error ) );
    assert( LoadSession( directory, loaded, error ) );
    const auto wire = ToWire( loaded.events[0] );
    const auto restored = FromWire( wire );
    assert( restored.label == loaded.events[0].label );
    assert( restored.frameLinks.size() == 1 );
    assert( restored.frameLinks[0].seriesId == 7 );
    const auto presentWire = FromWire( ToWire( loaded.events[1] ) );
    assert( presentWire.swapChainId == 0x1234 );
    assert( presentWire.displayStatus == DisplayStatus::Displayed );
    assert( presentWire.gpuBusyNs == 0 );
    assert( !FromWire( ToWire( loaded.events[2] ) ).displayLatencyNs );
    WireBatch batch;
    batch.qpcFrequency = 10'000'000;
    batch.processId = process.processId;
    batch.processStart100ns = process.startTime100ns;
    batch.count = 1;
    batch.events[0] = ToWire( loaded.events[0] );
    batch.events[0].eventId = 1;
    assert( ValidWireBatch( batch, process.processId ) );
    batch.events[0].processStart100ns++;
    assert( !ValidWireBatch( batch, process.processId ) );
    batch.events[0].processStart100ns--;
    batch.events[0].kind = 255;
    assert( !ValidWireBatch( batch, process.processId ) );
    batch.events[0].kind = static_cast<uint8_t>( loaded.events[0].kind );
    batch.count = LiveBatchCapacity + 1;
    assert( !ValidWireBatch( batch, process.processId ) );

    {
        FrameBridge bridge( directory / "frame-anchors.jsonl" );
        assert( bridge.Healthy() );
        for( uint64_t frame = 1; frame <= 100; ++frame )
            assert( bridge.Push( { frame, frame * 100, frame * 100 + 1 } ) );
        assert( bridge.Push( { 1, 20'000, 20'001, 7 } ) );
        assert( bridge.Dropped() == 0 );
    }
    std::vector<FrameAnchorRecord> anchors;
    assert( LoadFrameAnchors( directory / "frame-anchors.jsonl", anchors, error ) );
    assert( anchors.size() == 101 );
    assert( anchors.back().frameId == 1 );
    assert( anchors.back().seriesId == 7 );

    std::ofstream broken( directory / loaded.manifest.eventsFile, std::ios::app );
    broken << "{\"version\":9}\n";
    broken.close();
    assert( !LoadSession( directory, loaded, error ) );
    assert( error == "Invalid event at line 6" );
    std::filesystem::remove( directory / "capture.tracy" );
    std::ofstream( directory / loaded.manifest.eventsFile, std::ios::trunc );
    assert( !LoadSession( directory, loaded, error ) );
    assert( error == "Referenced Tracy capture is missing or outside the session" );
    std::filesystem::remove_all( directory );
}
