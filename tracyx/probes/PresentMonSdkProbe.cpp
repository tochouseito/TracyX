#include <PresentMonAPI.h>
#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>

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
        Sleep( 5000 );
        const auto stopped = pmStopTrackingProcess( session, pid );
        std::printf( "pmStopTrackingProcess=%d\n", static_cast<int>( stopped ) );
    }
    const auto closed = pmCloseSession( session );
    std::printf( "pmCloseSession=%d\n", static_cast<int>( closed ) );
    return tracked == PM_STATUS_SUCCESS && closed == PM_STATUS_SUCCESS ? 0 : 1;
}
