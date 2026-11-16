#include <tracy/Tracy.hpp>
#include <tracy/TracyD3D12.hpp>
#include "tracyx/FrameBridge.hpp"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#ifdef TRACYX_HAS_PIX
#include <WinPixEventRuntime/pix3.h>
#endif

#include <array>
#include <cstring>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{

constexpr UINT BufferSize = 4096;
constexpr size_t QueueCount = 3;
constexpr size_t FrameCount = 3;

bool Check( HRESULT result, const char* operation )
{
    if( SUCCEEDED( result ) ) return true;
    std::fprintf( stderr, "%s failed: 0x%08X\n", operation, static_cast<unsigned>( result ) );
    return false;
}

struct Queue
{
    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>, FrameCount> lists;
    std::array<uint64_t, FrameCount> submitted{};
    uint64_t nextFence = 1;
};

bool WaitForFence( ID3D12Fence* fence, uint64_t value, HANDLE ready )
{
    if( value == 0 || fence->GetCompletedValue() >= value ) return true;
    if( !Check( fence->SetEventOnCompletion( value, ready ), "SetEventOnCompletion" ) ) return false;
    return WaitForSingleObject( ready, 10'000 ) == WAIT_OBJECT_0;
}

bool CreateBuffer( ID3D12Device* device, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state,
    ComPtr<ID3D12Resource>& resource )
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heapType;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = BufferSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return Check( device->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc,
        state, nullptr, IID_PPV_ARGS( &resource ) ), "CreateCommittedResource" );
}

} // namespace

int main( int argc, char** argv )
{
    const int frames = argc > 1 ? std::atoi( argv[1] ) : 120;
    if( frames <= 0 || frames > 10'000 ) return 2;

    ComPtr<IDXGIFactory6> factory;
    ComPtr<ID3D12Device> device;
    if( !Check( CreateDXGIFactory2( 0, IID_PPV_ARGS( &factory ) ), "CreateDXGIFactory2" ) ) return 1;
    if( FAILED( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &device ) ) ) )
    {
        ComPtr<IDXGIAdapter> warp;
        if( !Check( factory->EnumWarpAdapter( IID_PPV_ARGS( &warp ) ), "EnumWarpAdapter" ) ||
            !Check( D3D12CreateDevice( warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &device ) ), "D3D12CreateDevice" ) ) return 1;
    }

    std::array<Queue, QueueCount> queues;
    queues[0].type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queues[1].type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    queues[2].type = D3D12_COMMAND_LIST_TYPE_COPY;
    for( auto& item : queues )
    {
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = item.type;
        if( !Check( device->CreateCommandQueue( &desc, IID_PPV_ARGS( &item.queue ) ), "CreateCommandQueue" ) ||
            !Check( device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &item.fence ) ), "CreateFence" ) ) return 1;
        for( size_t slot = 0; slot < FrameCount; ++slot )
        {
            if( !Check( device->CreateCommandAllocator( item.type, IID_PPV_ARGS( &item.allocators[slot] ) ), "CreateCommandAllocator" ) ||
                !Check( device->CreateCommandList( 0, item.type, item.allocators[slot].Get(), nullptr,
                    IID_PPV_ARGS( &item.lists[slot] ) ), "CreateCommandList" ) ||
                !Check( item.lists[slot]->Close(), "Close initial list" ) ) return 1;
        }
    }

    ComPtr<ID3D12Resource> upload;
    if( !CreateBuffer( device.Get(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, upload ) ) return 1;
    std::array<ComPtr<ID3D12Resource>, QueueCount> targets;
    for( auto& target : targets )
    {
        if( !CreateBuffer( device.Get(), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, target ) ) return 1;
    }
    void* mapped = nullptr;
    if( !Check( upload->Map( 0, nullptr, &mapped ), "Map" ) ) return 1;
    std::memset( mapped, 0x5A, BufferSize );
    upload->Unmap( 0, nullptr );

    auto* gpuContext = TracyD3D12Context( device.Get(), queues[0].queue.Get() );
    const auto ready = CreateEventW( nullptr, FALSE, FALSE, nullptr );
    if( !ready ) return 1;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency( &frequency );
    const auto anchorDirectory = argc > 2 ? std::filesystem::path( argv[2] ) : std::filesystem::current_path();
    std::filesystem::create_directories( anchorDirectory );
    tracyx::FrameBridge bridge( anchorDirectory / "frame-anchors.jsonl" );
    if( !bridge.Healthy() ) return 1;
    std::printf( "TracyX D3D12 probe PID=%lu queues=3 in-flight=%zu QPC=%lld\n",
        GetCurrentProcessId(), FrameCount, frequency.QuadPart );

    for( int frame = 1; frame <= frames; ++frame )
    {
        ZoneScopedN( "D3D12 frame submission" );
#ifdef TRACYX_HAS_PIX
        PIXBeginEvent( PIX_COLOR( 80, 160, 255 ), "Frame %d", frame );
#endif
        LARGE_INTEGER before{}, after{};
        QueryPerformanceCounter( &before );
        FrameMark;
        QueryPerformanceCounter( &after );
        if( !bridge.Push( { static_cast<uint64_t>( frame ), static_cast<uint64_t>( before.QuadPart ),
            static_cast<uint64_t>( after.QuadPart ) } ) )
            std::fprintf( stderr, "Frame Bridge dropped frame %d\n", frame );

        const auto slot = static_cast<size_t>( frame - 1 ) % FrameCount;
        for( size_t queueIndex = 0; queueIndex < QueueCount; ++queueIndex )
        {
            auto& item = queues[queueIndex];
            if( !WaitForFence( item.fence.Get(), item.submitted[slot], ready ) ) return 1;
            if( !Check( item.allocators[slot]->Reset(), "Allocator Reset" ) ||
                !Check( item.lists[slot]->Reset( item.allocators[slot].Get(), nullptr ), "List Reset" ) ) return 1;
            auto* list = item.lists[slot].Get();
#ifdef TRACYX_HAS_PIX
            PIXBeginEvent( list, PIX_COLOR( 80, 160, 255 ), "Queue %zu copy", queueIndex );
#endif
            if( queueIndex == 0 )
            {
                TracyD3D12Zone( gpuContext, list, "Graphics queue copy" );
                list->CopyBufferRegion( targets[queueIndex].Get(), 0, upload.Get(), 0, BufferSize );
            }
            else
            {
                list->CopyBufferRegion( targets[queueIndex].Get(), 0, upload.Get(), 0, BufferSize );
            }
#ifdef TRACYX_HAS_PIX
            PIXEndEvent( list );
#endif
            if( !Check( list->Close(), "List Close" ) ) return 1;
            ID3D12CommandList* submit[] = { list };
            item.queue->ExecuteCommandLists( 1, submit );
            item.submitted[slot] = item.nextFence++;
            if( !Check( item.queue->Signal( item.fence.Get(), item.submitted[slot] ), "Queue Signal" ) ) return 1;
        }
        if( frame % 30 == 0 ) std::printf( "frame=%d expected-submissions=%d\n", frame, frame * 3 );
#ifdef TRACYX_HAS_PIX
        PIXEndEvent();
#endif
        Sleep( 16 );
    }

    for( auto& item : queues )
    {
        if( !WaitForFence( item.fence.Get(), item.nextFence - 1, ready ) ) return 1;
    }
    TracyD3D12Collect( gpuContext );
    TracyD3D12Destroy( gpuContext );
    CloseHandle( ready );
    std::printf( "Finished %d frames and %d queue submissions. Bridge drops=%llu.\n",
        frames, frames * 3, static_cast<unsigned long long>( bridge.Dropped() ) );
}
