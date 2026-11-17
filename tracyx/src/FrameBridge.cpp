#include "tracyx/FrameBridge.hpp"

#include <chrono>

namespace tracyx
{

FrameBridge::FrameBridge( const std::filesystem::path& file )
    : m_output( file, std::ios::binary | std::ios::trunc )
{
    if( !m_output ) { m_failed.store( true ); return; }
    m_writer = std::thread( [this]() { Run(); } );
}

FrameBridge::~FrameBridge()
{
    {
        std::lock_guard lock( m_mutex );
        m_stopping = true;
    }
    m_ready.notify_one();
    if( m_writer.joinable() ) m_writer.join();
}

bool FrameBridge::Push( FrameAnchorRecord anchor )
{
    if( m_failed.load() || anchor.frameId == 0 || anchor.qpcAfter < anchor.qpcBefore ) return false;
    {
        std::lock_guard lock( m_mutex );
        if( m_queue.size() >= 4096 ) { m_dropped.fetch_add( 1 ); return false; }
        m_queue.push_back( anchor );
    }
    m_ready.notify_one();
    return true;
}

bool FrameBridge::Healthy() const
{
    return !m_failed.load();
}

uint64_t FrameBridge::Dropped() const
{
    return m_dropped.load();
}

void FrameBridge::Run()
{
    for( ;; )
    {
        std::deque<FrameAnchorRecord> batch;
        {
            std::unique_lock lock( m_mutex );
            m_ready.wait_for( lock, std::chrono::milliseconds( 50 ), [&]() { return m_stopping || !m_queue.empty(); } );
            if( m_queue.empty() && m_stopping ) break;
            batch.swap( m_queue );
        }
        for( const auto& anchor : batch )
        {
            m_output << "{\"frame_id\":" << anchor.frameId << ",\"qpc_before\":" << anchor.qpcBefore
                << ",\"qpc_after\":" << anchor.qpcAfter << ",\"series_id\":" << anchor.seriesId << "}\n";
        }
        m_output.flush();
        if( !m_output ) m_failed.store( true );
    }
}

} // namespace tracyx
