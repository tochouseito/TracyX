#include "PresentMonAdapter.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace tracyx
{
namespace
{

constexpr uint32_t BatchCapacity = 128;

std::optional<uint64_t> MillisecondsToNanoseconds( double milliseconds )
{
    if( !std::isfinite( milliseconds ) || milliseconds < 0 ) return std::nullopt;
    const auto nanoseconds = static_cast<long double>( milliseconds ) * 1'000'000.0L;
    if( nanoseconds > static_cast<long double>( std::numeric_limits<uint64_t>::max() ) ) return std::nullopt;
    return static_cast<uint64_t>( nanoseconds + 0.5L );
}

template<typename T>
T ReadMetric( const uint8_t* blob, const PM_QUERY_ELEMENT& element )
{
    T value{};
    std::memcpy( &value, blob + element.dataOffset, sizeof( T ) );
    return value;
}

std::string StatusMessage( const char* operation, PM_STATUS status )
{
    return std::string( operation ) + " failed with PresentMon status " + std::to_string( static_cast<int>( status ) );
}

} // namespace

PresentMonAdapter::~PresentMonAdapter()
{
    if( m_query ) pmFreeFrameQuery( m_query );
    if( m_session )
    {
        if( m_processId ) pmStopTrackingProcess( m_session, m_processId );
        pmCloseSession( m_session );
    }
}

bool PresentMonAdapter::Start( uint32_t processId, std::string& error )
{
    error.clear();
    auto status = pmOpenSession( &m_session );
    if( status != PM_STATUS_SUCCESS ) { error = StatusMessage( "pmOpenSession", status ); return false; }
    m_elements.resize( 7 );
    m_elements[0].metric = PM_METRIC_SWAP_CHAIN_ADDRESS;
    m_elements[1].metric = PM_METRIC_PRESENT_START_QPC;
    m_elements[2].metric = PM_METRIC_DROPPED_FRAMES;
    m_elements[3].metric = PM_METRIC_BETWEEN_DISPLAY_CHANGE;
    m_elements[4].metric = PM_METRIC_GPU_BUSY;
    m_elements[5].metric = PM_METRIC_DISPLAY_LATENCY;
    m_elements[6].metric = PM_METRIC_UNTIL_DISPLAYED;
    status = pmRegisterFrameQuery( m_session, &m_query, m_elements.data(), m_elements.size(), &m_blobSize );
    if( status != PM_STATUS_SUCCESS ) { error = StatusMessage( "pmRegisterFrameQuery", status ); return false; }
    if( m_blobSize == 0 || m_blobSize > 4096 || m_elements[0].dataSize != sizeof( uint64_t ) ||
        m_elements[1].dataSize != sizeof( uint64_t ) || m_elements[2].dataSize != sizeof( bool ) ||
        m_elements[3].dataSize != sizeof( double ) || m_elements[4].dataSize != sizeof( double ) ||
        m_elements[5].dataSize != sizeof( double ) || m_elements[6].dataSize != sizeof( double ) )
    {
        error = "PresentMon frame query returned unexpected metric sizes";
        return false;
    }
    for( const auto& element : m_elements )
    {
        if( element.dataOffset > m_blobSize || element.dataSize > m_blobSize - element.dataOffset )
        {
            error = "PresentMon frame query returned an out-of-range metric";
            return false;
        }
    }
    status = pmStartTrackingProcess( m_session, processId );
    if( status != PM_STATUS_SUCCESS ) { error = StatusMessage( "pmStartTrackingProcess", status ); return false; }
    m_processId = processId;
    return true;
}

bool PresentMonAdapter::Poll( std::vector<PresentMonFrame>& frames, std::string& error )
{
    error.clear();
    if( !m_query ) { error = "PresentMon frame query is unavailable"; return false; }
    std::vector<uint8_t> blobs( static_cast<size_t>( m_blobSize ) * BatchCapacity );
    for( int batch = 0; batch < 32; ++batch )
    {
        uint32_t count = BatchCapacity;
        const auto status = pmConsumeFrames( m_query, m_processId, blobs.data(), &count );
        if( status != PM_STATUS_SUCCESS ) { error = StatusMessage( "pmConsumeFrames", status ); return false; }
        for( uint32_t index = 0; index < count; ++index )
        {
            const auto* blob = blobs.data() + static_cast<size_t>( index ) * m_blobSize;
            PresentMonFrame frame;
            frame.swapChainId = ReadMetric<uint64_t>( blob, m_elements[0] );
            frame.presentQpc = ReadMetric<uint64_t>( blob, m_elements[1] );
            frame.dropped = ReadMetric<bool>( blob, m_elements[2] );
            frame.displayIntervalNs = MillisecondsToNanoseconds( ReadMetric<double>( blob, m_elements[3] ) );
            frame.gpuBusyNs = MillisecondsToNanoseconds( ReadMetric<double>( blob, m_elements[4] ) );
            frame.displayLatencyNs = MillisecondsToNanoseconds( ReadMetric<double>( blob, m_elements[5] ) );
            frame.untilDisplayedNs = MillisecondsToNanoseconds( ReadMetric<double>( blob, m_elements[6] ) );
            if( frame.presentQpc ) frames.push_back( frame );
        }
        if( count < BatchCapacity ) break;
    }
    return true;
}

} // namespace tracyx
