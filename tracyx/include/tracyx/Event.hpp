#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tracyx
{

enum class Source : uint8_t
{
    Bridge,
    DxEtw,
    PresentMon,
    Pix
};

enum class ClockDomain : uint8_t
{
    WindowsQpc,
    DxTimingNs,
    Tracy,
    Gpu,
    PresentMon
};

enum class EventKind : uint8_t
{
    FrameAnchor,
    GpuWork,
    QueueSubmit,
    Present,
    PsoCompile,
    Residency,
    RuntimeError,
    CustomCounter,
    Unknown,
    StorageRead,
    StorageSubmit,
    ResourceCreate,
    ResourceDestroy
};

enum class LinkStrength : uint8_t
{
    Explicit,
    TimeOverlap
};

enum class DisplayStatus : uint8_t
{
    Unknown,
    Displayed,
    Dropped
};

struct ProcessIdentity
{
    uint32_t processId = 0;
    uint64_t startTime100ns = 0;

    bool operator==( const ProcessIdentity& other ) const = default;
};

struct FrameLink
{
    uint64_t frameId = 0;
    LinkStrength strength = LinkStrength::TimeOverlap;
    uint64_t seriesId = 0;
};

struct Event
{
    uint64_t eventId = 0;
    Source source = Source::DxEtw;
    EventKind kind = EventKind::Unknown;
    ClockDomain clock = ClockDomain::WindowsQpc;
    uint64_t rawTimestamp = 0;
    std::optional<uint64_t> rawEndTimestamp;
    std::optional<uint64_t> normalizedNs;
    std::optional<uint64_t> normalizedEndNs;
    uint64_t uncertaintyNs = 0;
    ProcessIdentity process;
    uint32_t threadId = 0;
    uint64_t deviceId = 0;
    uint64_t queueId = 0;
    uint64_t commandListId = 0;
    uint64_t resourceId = 0;
    uint64_t swapChainId = 0;
    DisplayStatus displayStatus = DisplayStatus::Unknown;
    std::optional<uint64_t> displayIntervalNs;
    std::optional<uint64_t> gpuBusyNs;
    std::optional<uint64_t> displayLatencyNs;
    std::string label;
    std::vector<FrameLink> frameLinks;
};

} // namespace tracyx
