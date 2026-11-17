#pragma once

#include <PresentMonAPI.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tracyx
{

struct PresentMonFrame
{
    uint64_t swapChainId = 0;
    uint64_t presentQpc = 0;
    bool dropped = false;
    std::optional<uint64_t> displayIntervalNs;
    std::optional<uint64_t> gpuBusyNs;
    std::optional<uint64_t> displayLatencyNs;
    std::optional<uint64_t> untilDisplayedNs;
};

class PresentMonAdapter
{
public:
    ~PresentMonAdapter();
    bool Start( uint32_t processId, std::string& error );
    bool Poll( std::vector<PresentMonFrame>& frames, std::string& error );

private:
    PM_SESSION_HANDLE m_session = nullptr;
    PM_FRAME_QUERY_HANDLE m_query = nullptr;
    uint32_t m_processId = 0;
    uint32_t m_blobSize = 0;
    std::vector<PM_QUERY_ELEMENT> m_elements;
};

} // namespace tracyx
