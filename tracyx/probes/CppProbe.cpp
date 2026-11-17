#include <tracy/Tracy.hpp>
#include "tracyx/TracyMemory.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <vector>

namespace
{

void ExerciseMemory()
{
    tracyx::TracyMemoryPool heap( "TracyX.Probe.Heap" );
    void* first = std::malloc( 64 );
    void* second = std::malloc( 96 );
    assert( first && second );
    const auto firstRecorded = heap.Alloc( first, 64 );
    assert( firstRecorded );
    assert( heap.Snapshot().trackedBytes == 64 );
    const auto resized = heap.Realloc( first, second, 96 );
    assert( resized );
    std::free( first );
    assert( heap.Snapshot().trackedBytes == 96 );
    const auto failedResize = heap.Realloc( second, nullptr, 128 );
    assert( !failedResize );
    assert( heap.Snapshot().trackedBytes == 96 );
    const auto freed = heap.Free( second );
    assert( freed );
    std::free( second );
    assert( heap.Snapshot().outstandingAllocations == 0 );
    assert( heap.Snapshot().failedAllocations == 1 );

    std::vector<std::byte> backing( 256 );
    tracyx::TracyMemoryPool arena( "TracyX.Probe.Arena" );
    const auto firstArena = arena.Alloc( backing.data(), 32 );
    const auto secondArena = arena.Alloc( backing.data() + 32, 48 );
    assert( firstArena && secondArena );
    assert( arena.Snapshot().trackedBytes == 80 );
    arena.ResetArena();
    assert( arena.Snapshot().trackedBytes == 0 );
    assert( arena.Snapshot().outstandingAllocations == 0 );
    arena.PublishMetrics( { 256, 0, 256, 0.0, 0 } );

    std::atomic<bool> good{ true };
    std::vector<std::thread> workers;
    for( int worker = 0; worker < 4; ++worker )
        workers.emplace_back( [&]() {
            for( int i = 0; i < 100; ++i )
            {
                void* memory = std::malloc( 32 );
                if( !memory || !heap.Alloc( memory, 32 ) || !heap.Free( memory ) ) good.store( false );
                std::free( memory );
            }
        } );
    for( auto& worker : workers ) worker.join();
    assert( good.load() && heap.Snapshot().trackedBytes == 0 );
    std::printf( "Memory probe: heap outstanding 0, arena reset 0, failed allocations 1.\n" );
}

} // namespace

int main()
{
    std::printf( "TracyX general C++ probe: PID, zones and named memory pools; no FrameMark.\n" );
    ExerciseMemory();
    for( int sample = 0; sample < 120; ++sample )
    {
        ZoneScopedN( "General C++ work" );
        uint64_t sum = 0;
        for( uint64_t i = 0; i < 100'000; ++i ) sum += i ^ sample;
        if( sample % 30 == 0 ) std::printf( "sample=%d sum=%llu\n", sample, static_cast<unsigned long long>( sum ) );
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
}
