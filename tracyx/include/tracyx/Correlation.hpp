#pragma once

#include "tracyx/Event.hpp"

#include <vector>

namespace tracyx
{

struct FrameSpan
{
    uint64_t frameId = 0;
    ProcessIdentity process;
    uint64_t beginNs = 0;
    uint64_t endNs = 0;
    uint64_t seriesId = 0;
};

class FrameCorrelator
{
public:
    bool AddFrame( FrameSpan frame );
    std::vector<FrameLink> FindLinks( const Event& event ) const;

private:
    std::vector<FrameSpan> m_frames;
};

} // namespace tracyx
