#pragma once

#include "tracyx/Session.hpp"
#include "tracyx/Diagnostics.hpp"
#include "tracyx/Comparison.hpp"
#include "tracyx/LiveProtocol.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tracy { class Worker; struct FrameData; }

namespace tracy::tracyx_viewer
{

class Panel
{
public:
    ~Panel();
    void Draw( const std::string& traceFile, Worker& worker, const FrameData* frames,
        int64_t viewBegin, int64_t viewEnd, int frameHover,
        const std::function<void( int64_t, int64_t )>& zoom );

private:
    struct AlignedEvent
    {
        size_t index = 0;
        int64_t begin = 0;
        int64_t end = 0;
        uint64_t uncertainty = 0;
        std::vector<tracyx::FrameLink> frameLinks;
    };

    void Load( const std::filesystem::path& directory );
    void Align( Worker& worker, const FrameData* frames );
    void StartLive( uint32_t pid );
    void PollLive( Worker& worker, const FrameData* frames );
    void RefreshPixLinks();
    void OpenPixCapture( size_t index );

    tracyx::Session m_session;
    std::vector<AlignedEvent> m_aligned;
    std::vector<uint64_t> m_frameIdByIndex;
    const FrameData* m_lastFrames = nullptr;
    std::array<char, 1024> m_path{};
    std::array<char, 1024> m_pixPath{};
    std::array<char, 1024> m_comparePath{};
    std::filesystem::path m_sessionDirectory;
    std::vector<tracyx::PixCaptureRecord> m_pixCaptures;
    std::vector<std::vector<tracyx::FrameLink>> m_pixLinks;
    tracyx::FrameDiagnosticReport m_diagnosticReport;
    tracyx::SessionComparison m_comparison;
    bool m_hasComparison = false;
    std::string m_comparisonError;
    uint64_t m_lastHoverFrameId = 0;
    std::string m_traceFile;
    std::string m_status;
    size_t m_selected = 0;
    size_t m_hovered = 0;
    bool m_inspectorExpanded = false;
    uint64_t m_residual = 0;
    uint64_t m_seriesId = 0;
    size_t m_anchorCount = 0;
    size_t m_unaligned = 0;
    size_t m_processMismatch = 0;
    size_t m_unconvertibleClock = 0;
    size_t m_outsideAnchors = 0;
    bool m_loaded = false;
    bool m_autoLoadChecked = false;
    bool m_livePositioned = false;
    bool m_savedPositioned = false;
    uint32_t m_livePid = 0;
    uint64_t m_liveLastEventId = 0;
    uint64_t m_liveBacklog = 0;
    std::atomic<uint64_t> m_viewerDrops{ 0 };
    size_t m_liveLastAnchorCount = 0;
    size_t m_lastAlignedCount = 0;
    std::chrono::steady_clock::time_point m_lastAlignment{};
    std::atomic<bool> m_liveRunning{ false };
    std::atomic<bool> m_liveDisconnected{ false };
    std::thread m_liveThread;
    std::mutex m_liveMutex;
    std::string m_liveError;
    std::deque<tracyx::WireBatch> m_liveQueue;
};

} // namespace tracy::tracyx_viewer
