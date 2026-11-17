#include <PresentMonAPI.h>
#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <array>
#include <cstring>
#include <vector>

int main( int argc, char** argv )
{
    if( argc != 2 )
    {
        std::fprintf( stderr, "Usage: TracyXProbePresentMon <target-pid>\n" );
        return 2;
    }
    char* end = nullptr;
    const auto parsed = std::strtoul( argv[1], &end, 10 );
    if( end == argv[1] || *end != '\0' || parsed == 0 || parsed > UINT32_MAX ) return 2;
    const auto pid = static_cast<uint32_t>( parsed );

    PM_SESSION_HANDLE session = nullptr;
    const auto opened = pmOpenSession( &session );
    std::printf( "pmOpenSession=%d\n", static_cast<int>( opened ) );
    if( opened != PM_STATUS_SUCCESS ) return 1;
    const auto tracked = pmStartTrackingProcess( session, pid );
    std::printf( "pmStartTrackingProcess=%d pid=%u\n", static_cast<int>( tracked ), pid );
    if( tracked == PM_STATUS_SUCCESS )
    {
        std::array<PM_QUERY_ELEMENT, 7> elements{};
        elements[0].metric = PM_METRIC_SWAP_CHAIN_ADDRESS;
        elements[1].metric = PM_METRIC_PRESENT_START_QPC;
        elements[2].metric = PM_METRIC_DROPPED_FRAMES;
        elements[3].metric = PM_METRIC_BETWEEN_DISPLAY_CHANGE;
        elements[4].metric = PM_METRIC_GPU_BUSY;
        elements[5].metric = PM_METRIC_DISPLAY_LATENCY;
        elements[6].metric = PM_METRIC_UNTIL_DISPLAYED;
        PM_FRAME_QUERY_HANDLE query = nullptr;
        uint32_t blobSize = 0;
        const auto registered = pmRegisterFrameQuery( session, &query, elements.data(), elements.size(), &blobSize );
        std::printf( "pmRegisterFrameQuery=%d blob_size=%u\n", static_cast<int>( registered ), blobSize );
        uint64_t frameCount = 0;
        if( registered == PM_STATUS_SUCCESS && blobSize > 0 )
        {
            std::vector<uint8_t> blobs( static_cast<size_t>( blobSize ) * 128 );
            for( int poll = 0; poll < 50; ++poll )
            {
                uint32_t available = 128;
                const auto consumed = pmConsumeFrames( query, pid, blobs.data(), &available );
                if( consumed != PM_STATUS_SUCCESS )
                {
                    std::printf( "pmConsumeFrames=%d\n", static_cast<int>( consumed ) );
                    break;
                }
                for( uint32_t index = 0; index < available; ++index )
                {
                    const auto* blob = blobs.data() + static_cast<size_t>( index ) * blobSize;
                    uint64_t swapChain = 0, presentQpc = 0;
                    std::memcpy( &swapChain, blob + elements[0].dataOffset, sizeof( swapChain ) );
                    std::memcpy( &presentQpc, blob + elements[1].dataOffset, sizeof( presentQpc ) );
                    if( frameCount < 5 )
                        std::printf( "frame=%llu swap-chain=%llx present-qpc=%llu\n",
                            static_cast<unsigned long long>( frameCount ),
                            static_cast<unsigned long long>( swapChain ),
                            static_cast<unsigned long long>( presentQpc ) );
                    ++frameCount;
                }
                Sleep( 100 );
            }
            pmFreeFrameQuery( query );
        }
        std::printf( "frame_query_count=%llu\n", static_cast<unsigned long long>( frameCount ) );
        const auto stopped = pmStopTrackingProcess( session, pid );
        std::printf( "pmStopTrackingProcess=%d\n", static_cast<int>( stopped ) );
    }
    const auto closed = pmCloseSession( session );
    std::printf( "pmCloseSession=%d\n", static_cast<int>( closed ) );
    return tracked == PM_STATUS_SUCCESS && closed == PM_STATUS_SUCCESS ? 0 : 1;
}
