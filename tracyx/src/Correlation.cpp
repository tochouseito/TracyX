#include "tracyx/Correlation.hpp"

#include <algorithm>
#include <limits>

namespace tracyx
{

bool FrameCorrelator::AddFrame( FrameSpan frame )
{
    if( frame.frameId == 0 || frame.beginNs > frame.endNs ) return false;
    const auto duplicate = std::find_if( m_frames.begin(), m_frames.end(), [&]( const FrameSpan& other ) {
        return other.frameId == frame.frameId && other.seriesId == frame.seriesId && other.process == frame.process;
    } );
    if( duplicate != m_frames.end() ) return false;
    m_frames.push_back( frame );
    return true;
}

std::vector<FrameLink> FrameCorrelator::FindLinks( const Event& event ) const
{
    if( !event.frameLinks.empty() ) return event.frameLinks;
    if( !event.normalizedNs ) return {};

    const auto begin = *event.normalizedNs > event.uncertaintyNs
        ? *event.normalizedNs - event.uncertaintyNs : 0;
    const auto eventEnd = event.normalizedEndNs.value_or( *event.normalizedNs );
    const auto end = event.uncertaintyNs > std::numeric_limits<uint64_t>::max() - eventEnd
        ? std::numeric_limits<uint64_t>::max() : eventEnd + event.uncertaintyNs;

    std::vector<FrameLink> links;
    for( const auto& frame : m_frames )
    {
        if( frame.process == event.process && begin <= frame.endNs && end >= frame.beginNs )
        {
            links.push_back( { frame.frameId, LinkStrength::TimeOverlap, frame.seriesId } );
        }
    }
    return links;
}

} // namespace tracyx
