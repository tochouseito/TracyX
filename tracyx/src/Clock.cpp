#include "tracyx/Clock.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tracyx
{

QpcClock::QpcClock( uint64_t frequency, uint64_t origin )
    : m_frequency( frequency )
    , m_origin( origin )
{
}

std::optional<NormalizedTime> QpcClock::Normalize( uint64_t qpc ) const
{
    if( m_frequency == 0 || qpc < m_origin ) return std::nullopt;

    const auto seconds = static_cast<long double>( qpc - m_origin ) / m_frequency;
    const auto ns = seconds * 1'000'000'000.0L;
    if( ns > static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) return std::nullopt;
    return NormalizedTime{ static_cast<uint64_t>( ns + 0.5L ), 1 };
}

TracyClockFit::TracyClockFit( uint64_t qpcFrequency )
    : m_frequency( qpcFrequency )
{
}

bool TracyClockFit::AddAnchor( ClockAnchor anchor )
{
    if( m_frequency == 0 || anchor.qpcBefore > anchor.qpcAfter ) return false;
    if( !m_anchors.empty() && anchor.qpcBefore <= m_anchors.back().qpcAfter ) return false;
    m_anchors.push_back( anchor );
    return true;
}

std::optional<NormalizedTime> TracyClockFit::ToTracyNs( uint64_t qpc ) const
{
    if( m_anchors.size() < 2 ) return std::nullopt;
    if( qpc < m_anchors.front().qpcBefore || qpc > m_anchors.back().qpcAfter ) return std::nullopt;

    const auto midpoint = []( const ClockAnchor& anchor ) -> long double {
        return static_cast<long double>( anchor.qpcBefore ) / 2 + static_cast<long double>( anchor.qpcAfter ) / 2;
    };
    const auto upper = std::upper_bound( m_anchors.begin(), m_anchors.end(), static_cast<long double>( qpc ),
        [&]( long double value, const ClockAnchor& anchor ) { return value < midpoint( anchor ); } );
    const auto right = upper == m_anchors.begin() ? m_anchors.begin() + 1 : upper == m_anchors.end() ? m_anchors.end() - 1 : upper;
    const auto left = right - 1;
    const auto x0 = midpoint( *left );
    const auto x1 = midpoint( *right );
    if( x1 <= x0 || right->tracyNs <= left->tracyNs ) return std::nullopt;
    const auto slope = static_cast<long double>( right->tracyNs - left->tracyNs ) / ( x1 - x0 );
    const auto estimate = static_cast<long double>( left->tracyNs ) + ( static_cast<long double>( qpc ) - x0 ) * slope;
    if( estimate < 0 || estimate > static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) return std::nullopt;
    const auto halfWidth = std::max( left->qpcAfter - left->qpcBefore, right->qpcAfter - right->qpcBefore ) / 2;
    const auto anchorError = static_cast<uint64_t>( std::ceil( halfWidth * slope ) );
    return NormalizedTime{ static_cast<uint64_t>( estimate + 0.5L ), anchorError + 1 };
}

uint64_t TracyClockFit::MaxResidualNs() const
{
    if( m_anchors.size() < 2 ) return 0;
    const auto& first = m_anchors.front();
    const auto& last = m_anchors.back();
    const auto x0 = static_cast<long double>( first.qpcBefore ) / 2 + static_cast<long double>( first.qpcAfter ) / 2;
    const auto x1 = static_cast<long double>( last.qpcBefore ) / 2 + static_cast<long double>( last.qpcAfter ) / 2;
    if( x1 <= x0 || last.tracyNs <= first.tracyNs ) return 0;
    const auto slope = static_cast<long double>( last.tracyNs - first.tracyNs ) / ( x1 - x0 );
    long double maximum = 0;
    for( const auto& anchor : m_anchors )
    {
        const auto x = static_cast<long double>( anchor.qpcBefore ) / 2 + static_cast<long double>( anchor.qpcAfter ) / 2;
        const auto expected = static_cast<long double>( first.tracyNs ) + ( x - x0 ) * slope;
        maximum = std::max( maximum, std::abs( expected - anchor.tracyNs ) );
    }
    return static_cast<uint64_t>( std::ceil( maximum ) );
}

size_t TracyClockFit::AnchorCount() const
{
    return m_anchors.size();
}

} // namespace tracyx
