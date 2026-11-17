#pragma once

#include "tracyx/Event.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace tracyx
{

inline constexpr uint32_t LiveMagic = 0x58594354; // TCYX
inline constexpr uint32_t LiveVersion = 4;
inline constexpr size_t LiveBatchCapacity = 128;

struct WireEvent
{
    uint64_t eventId = 0;
    uint64_t rawTimestamp = 0;
    uint64_t rawEndTimestamp = 0;
    uint64_t normalizedNs = 0;
    uint64_t normalizedEndNs = 0;
    uint64_t uncertaintyNs = 0;
    uint64_t processStart100ns = 0;
    uint64_t deviceId = 0;
    uint64_t queueId = 0;
    uint64_t commandListId = 0;
    uint64_t resourceId = 0;
    uint64_t swapChainId = 0;
    uint64_t displayIntervalNs = 0;
    uint64_t gpuBusyNs = 0;
    uint64_t displayLatencyNs = 0;
    uint64_t frameId = 0;
    uint64_t frameSeriesId = 0;
    uint32_t processId = 0;
    uint32_t threadId = 0;
    uint8_t source = 0;
    uint8_t kind = 0;
    uint8_t clock = 0;
    uint8_t flags = 0;
    uint8_t displayStatus = 0;
    char label[96]{};
};

struct WireBatch
{
    uint32_t magic = LiveMagic;
    uint32_t version = LiveVersion;
    uint32_t count = 0;
    uint32_t reserved = 0;
    uint64_t qpcFrequency = 0;
    uint64_t qpcOrigin = 0;
    uint64_t processStart100ns = 0;
    uint64_t lostEvents = 0;
    uint64_t queueDrops = 0;
    uint64_t bridgeMissing = 0;
    uint64_t d3d12Events = 0;
    uint64_t dxgkEvents = 0;
    uint64_t d3d12DecodeErrors = 0;
    uint64_t dxgkDecodeErrors = 0;
    uint64_t presentMonFrames = 0;
    uint64_t presentMonDropped = 0;
    uint64_t backlog = 0;
    uint32_t processId = 0;
    uint32_t presentMonState = 0;
    char presentMonError[96]{};
    WireEvent events[LiveBatchCapacity]{};
};

static_assert( std::is_standard_layout_v<WireBatch> );

inline WireEvent ToWire( const Event& event )
{
    WireEvent wire;
    wire.eventId = event.eventId;
    wire.rawTimestamp = event.rawTimestamp;
    wire.rawEndTimestamp = event.rawEndTimestamp.value_or( 0 );
    wire.normalizedNs = event.normalizedNs.value_or( 0 );
    wire.normalizedEndNs = event.normalizedEndNs.value_or( 0 );
    wire.uncertaintyNs = event.uncertaintyNs;
    wire.processId = event.process.processId;
    wire.processStart100ns = event.process.startTime100ns;
    wire.threadId = event.threadId;
    wire.deviceId = event.deviceId;
    wire.queueId = event.queueId;
    wire.commandListId = event.commandListId;
    wire.resourceId = event.resourceId;
    wire.swapChainId = event.swapChainId;
    wire.displayIntervalNs = event.displayIntervalNs.value_or( 0 );
    wire.gpuBusyNs = event.gpuBusyNs.value_or( 0 );
    wire.displayLatencyNs = event.displayLatencyNs.value_or( 0 );
    wire.displayStatus = static_cast<uint8_t>( event.displayStatus );
    wire.source = static_cast<uint8_t>( event.source );
    wire.kind = static_cast<uint8_t>( event.kind );
    wire.clock = static_cast<uint8_t>( event.clock );
    wire.flags = static_cast<uint8_t>( ( event.rawEndTimestamp ? 1 : 0 ) |
        ( event.normalizedNs ? 2 : 0 ) | ( event.normalizedEndNs ? 4 : 0 ) |
        ( event.displayIntervalNs ? 32 : 0 ) | ( event.gpuBusyNs ? 64 : 0 ) |
        ( event.displayLatencyNs ? 128 : 0 ) );
    if( !event.frameLinks.empty() )
    {
        wire.frameId = event.frameLinks.front().frameId;
        wire.frameSeriesId = event.frameLinks.front().seriesId;
        wire.flags |= event.frameLinks.front().strength == LinkStrength::Explicit ? 8 : 16;
    }
    std::memcpy( wire.label, event.label.data(), std::min( event.label.size(), sizeof( wire.label ) - 1 ) );
    return wire;
}

inline Event FromWire( const WireEvent& wire )
{
    Event event;
    event.eventId = wire.eventId;
    event.rawTimestamp = wire.rawTimestamp;
    if( wire.flags & 1 ) event.rawEndTimestamp = wire.rawEndTimestamp;
    if( wire.flags & 2 ) event.normalizedNs = wire.normalizedNs;
    if( wire.flags & 4 ) event.normalizedEndNs = wire.normalizedEndNs;
    event.uncertaintyNs = wire.uncertaintyNs;
    event.process = { wire.processId, wire.processStart100ns };
    event.threadId = wire.threadId;
    event.deviceId = wire.deviceId;
    event.queueId = wire.queueId;
    event.commandListId = wire.commandListId;
    event.resourceId = wire.resourceId;
    event.swapChainId = wire.swapChainId;
    event.displayStatus = static_cast<DisplayStatus>( wire.displayStatus );
    if( wire.flags & 32 ) event.displayIntervalNs = wire.displayIntervalNs;
    if( wire.flags & 64 ) event.gpuBusyNs = wire.gpuBusyNs;
    if( wire.flags & 128 ) event.displayLatencyNs = wire.displayLatencyNs;
    event.source = static_cast<Source>( wire.source );
    event.kind = static_cast<EventKind>( wire.kind );
    event.clock = static_cast<ClockDomain>( wire.clock );
    if( wire.frameId ) event.frameLinks.push_back( { wire.frameId,
        ( wire.flags & 8 ) ? LinkStrength::Explicit : LinkStrength::TimeOverlap, wire.frameSeriesId } );
    size_t labelLength = 0;
    while( labelLength < sizeof( wire.label ) && wire.label[labelLength] != '\0' ) ++labelLength;
    event.label.assign( wire.label, labelLength );
    return event;
}

} // namespace tracyx
