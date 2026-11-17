#include <tracy/Tracy.hpp>

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <thread>

int main()
{
    std::printf( "TracyX general C++ probe: PID and zones only; no FrameMark.\n" );
    for( int sample = 0; sample < 120; ++sample )
    {
        ZoneScopedN( "General C++ work" );
        uint64_t sum = 0;
        for( uint64_t i = 0; i < 100'000; ++i ) sum += i ^ sample;
        if( sample % 30 == 0 ) std::printf( "sample=%d sum=%llu\n", sample, static_cast<unsigned long long>( sum ) );
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
}
