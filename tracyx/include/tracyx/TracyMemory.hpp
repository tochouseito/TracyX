#pragma once

#include <tracy/Tracy.hpp>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace tracyx
{

struct MemorySnapshot
{
    uint64_t trackedBytes = 0;
    uint64_t outstandingAllocations = 0;
    uint64_t failedAllocations = 0;
};

struct PoolMetrics
{
    uint64_t reservedBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t freeBytes = 0;
    double fragmentationPercent = 0;
    uint64_t failedAllocations = 0;
};

class TracyMemoryPool
{
public:
    explicit TracyMemoryPool( std::string name )
        : m_name( std::move( name ) )
        , m_reservedPlot( m_name + ".reserved" )
        , m_usedPlot( m_name + ".used" )
        , m_freePlot( m_name + ".free" )
        , m_fragmentationPlot( m_name + ".fragmentation_pct" )
        , m_failedPlot( m_name + ".failed_allocations" )
    {
    }

    TracyMemoryPool( const TracyMemoryPool& ) = delete;
    TracyMemoryPool& operator=( const TracyMemoryPool& ) = delete;

    bool Alloc( const void* pointer, size_t bytes )
    {
        std::lock_guard lock( m_mutex );
        if( !pointer ) { ++m_failedAllocations; return false; }
        if( m_allocations.contains( pointer ) ) return false;
        m_allocations.emplace( pointer, bytes );
        m_trackedBytes += bytes;
        TracyAllocN( pointer, bytes, m_name.c_str() );
        return true;
    }

    bool Free( const void* pointer )
    {
        std::lock_guard lock( m_mutex );
        const auto found = m_allocations.find( pointer );
        if( found == m_allocations.end() ) return false;
        TracyFreeN( pointer, m_name.c_str() );
        m_trackedBytes -= found->second;
        m_allocations.erase( found );
        return true;
    }

    bool Realloc( const void* oldPointer, const void* newPointer, size_t newBytes )
    {
        std::lock_guard lock( m_mutex );
        if( !newPointer ) { ++m_failedAllocations; return false; }
        if( oldPointer && !m_allocations.contains( oldPointer ) ) return false;
        if( newPointer != oldPointer && m_allocations.contains( newPointer ) ) return false;
        if( oldPointer )
        {
            const auto found = m_allocations.find( oldPointer );
            TracyFreeN( oldPointer, m_name.c_str() );
            m_trackedBytes -= found->second;
            m_allocations.erase( found );
        }
        m_allocations.emplace( newPointer, newBytes );
        m_trackedBytes += newBytes;
        TracyAllocN( newPointer, newBytes, m_name.c_str() );
        return true;
    }

    void ResetArena()
    {
        std::lock_guard lock( m_mutex );
        TracyMemoryDiscard( m_name.c_str() );
        m_allocations.clear();
        m_trackedBytes = 0;
    }

    MemorySnapshot Snapshot() const
    {
        std::lock_guard lock( m_mutex );
        return { m_trackedBytes, m_allocations.size(), m_failedAllocations };
    }

    void PublishMetrics( const PoolMetrics& metrics ) const
    {
        TracyPlot( m_reservedPlot.c_str(), static_cast<int64_t>( metrics.reservedBytes ) );
        TracyPlot( m_usedPlot.c_str(), static_cast<int64_t>( metrics.usedBytes ) );
        TracyPlot( m_freePlot.c_str(), static_cast<int64_t>( metrics.freeBytes ) );
        TracyPlot( m_fragmentationPlot.c_str(), metrics.fragmentationPercent );
        TracyPlot( m_failedPlot.c_str(), static_cast<int64_t>( metrics.failedAllocations ) );
    }

private:
    const std::string m_name;
    const std::string m_reservedPlot;
    const std::string m_usedPlot;
    const std::string m_freePlot;
    const std::string m_fragmentationPlot;
    const std::string m_failedPlot;
    mutable std::mutex m_mutex;
    std::unordered_map<const void*, size_t> m_allocations;
    uint64_t m_trackedBytes = 0;
    uint64_t m_failedAllocations = 0;
};

} // namespace tracyx
