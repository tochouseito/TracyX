#pragma once

#include "tracyx/Session.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace tracyx
{

// FrameMark の呼び出し側ではメモリ Queue に載せ、ディスク書込は専用 Thread で行う。
class FrameBridge
{
public:
    explicit FrameBridge( const std::filesystem::path& file );
    ~FrameBridge();

    FrameBridge( const FrameBridge& ) = delete;
    FrameBridge& operator=( const FrameBridge& ) = delete;

    bool Push( FrameAnchorRecord anchor );
    bool Healthy() const;
    uint64_t Dropped() const;

private:
    void Run();

    std::ofstream m_output;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<FrameAnchorRecord> m_queue;
    std::thread m_writer;
    std::atomic<uint64_t> m_dropped{ 0 };
    std::atomic<bool> m_failed{ false };
    bool m_stopping = false;
};

} // namespace tracyx
