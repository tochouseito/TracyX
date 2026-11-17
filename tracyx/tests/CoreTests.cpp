#include "tracyx/Clock.hpp"
#include "tracyx/Correlation.hpp"
#include "tracyx/FrameBridge.hpp"
#include "tracyx/LiveProtocol.hpp"
#include "tracyx/Session.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

int main()
{
    using namespace tracyx;

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

    const auto directory = std::filesystem::temp_directory_path() / "tracyx-core-test";
    std::filesystem::create_directories( directory );
    Session saved;
    saved.manifest.process = process;
    saved.manifest.qpcFrequency = 10'000'000;
    saved.manifest.tracyFile = "capture.tracy";
    saved.manifest.presentMonState = 1;
    saved.manifest.presentMonFrames = 2;
    saved.manifest.presentMonDropped = 1;
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
    std::string error;
    assert( SaveSession( directory, saved, error ) );
    Session loaded;
    assert( LoadSession( directory, loaded, error ) );
    assert( loaded.manifest.process == process );
    assert( loaded.events.size() == 3 );
    assert( loaded.manifest.presentMonFrames == 2 );
    assert( loaded.manifest.presentMonDropped == 1 );
    assert( loaded.events[1].swapChainId == 0x1234 );
    assert( loaded.events[1].displayStatus == DisplayStatus::Displayed );
    assert( loaded.events[1].gpuBusyNs == 0 );
    assert( loaded.events[2].displayStatus == DisplayStatus::Dropped );
    assert( !loaded.events[2].rawEndTimestamp );
    assert( !loaded.events[2].displayIntervalNs );
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
    pix.fileName = "GPU \\\"test\\\".wpix";
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
    std::ofstream( directory / "events.jsonl", std::ios::trunc ).close();
    assert( !LoadSession( directory, loaded, error ) );
    assert( error == "Event count mismatch" );
    assert( SaveSession( directory, saved, error ) );
    {
        std::ofstream manifest( directory / "session.json", std::ios::trunc );
        manifest << "{\"version\":1,\"process_id\":42,\"process_start_100ns\":300,"
            "\"qpc_frequency\":10000000,\"qpc_origin\":0,\"lost_events\":0,"
            "\"queue_drops\":0,\"clock_residual_ns\":0,\"tracy_file\":\"capture.tracy\"}\n";
    }
    assert( !LoadSession( directory, loaded, error ) );
    assert( error == "Invalid or unsupported manifest" );
    assert( SaveSession( directory, saved, error ) );
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

    std::ofstream broken( directory / "events.jsonl", std::ios::app );
    broken << "{\"version\":9}\n";
    broken.close();
    assert( !LoadSession( directory, loaded, error ) );
    assert( error == "Invalid event at line 4" );
    std::filesystem::remove( directory / "capture.tracy" );
    std::ofstream( directory / "events.jsonl", std::ios::trunc );
    assert( !LoadSession( directory, loaded, error ) );
    assert( error == "Referenced Tracy capture is missing or outside the session" );
    std::filesystem::remove_all( directory );
}
