#include <tracy/Tracy.hpp>
#include "tracyx/FrameBridge.hpp"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using Microsoft::WRL::ComPtr;

namespace
{

constexpr size_t SwapChainCount = 2;

bool Check( HRESULT result, const char* operation )
{
    if( SUCCEEDED( result ) ) return true;
    std::fprintf( stderr, "%s failed: 0x%08X\n", operation, static_cast<unsigned>( result ) );
    return false;
}

} // namespace

int main( int argc, char** argv )
{
    const int frameCount = argc > 1 ? std::atoi( argv[1] ) : 180;
    if( frameCount <= 0 || frameCount > 10'000 ) return 2;
    const auto directory = argc > 2 ? std::filesystem::path( argv[2] ) : std::filesystem::current_path();
    std::filesystem::create_directories( directory );
    tracyx::FrameBridge bridge( directory / "frame-anchors.jsonl" );
    if( !bridge.Healthy() ) return 1;

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW( nullptr );
    windowClass.lpszClassName = L"TracyXPresentD3D12Probe";
    if( !RegisterClassW( &windowClass ) ) return 1;

    ComPtr<IDXGIFactory6> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    if( !Check( CreateDXGIFactory2( 0, IID_PPV_ARGS( &factory ) ), "CreateDXGIFactory2" ) ||
        !Check( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &device ) ), "D3D12CreateDevice" ) ) return 1;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if( !Check( device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &queue ) ), "CreateCommandQueue" ) ) return 1;

    std::array<HWND, SwapChainCount> windows{};
    std::array<ComPtr<IDXGISwapChain1>, SwapChainCount> swapChains;
    for( size_t index = 0; index < SwapChainCount; ++index )
    {
        windows[index] = CreateWindowExW( 0, windowClass.lpszClassName, L"TracyX Present Probe",
            WS_OVERLAPPEDWINDOW, 80 + static_cast<int>( index ) * 360, 80, 320, 240,
            nullptr, nullptr, windowClass.hInstance, nullptr );
        if( !windows[index] ) return 1;
        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        swapDesc.Width = 320;
        swapDesc.Height = 240;
        swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = 2;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if( !Check( factory->CreateSwapChainForHwnd( queue.Get(), windows[index], &swapDesc,
            nullptr, nullptr, &swapChains[index] ), "CreateSwapChainForHwnd" ) ) return 1;
        ShowWindow( windows[index], SW_SHOWNORMAL );
        ShowWindow( windows[index], SW_SHOW );
        UpdateWindow( windows[index] );
    }

    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency( &frequency );
    std::printf( "TracyX Present probe PID=%lu swap-chains=%zu QPC=%lld\n",
        GetCurrentProcessId(), SwapChainCount, frequency.QuadPart );
    for( int frame = 1; frame <= frameCount; ++frame )
    {
        ZoneScopedN( "D3D12 Present frame" );
        MSG message{};
        while( PeekMessageW( &message, nullptr, 0, 0, PM_REMOVE ) )
        {
            TranslateMessage( &message );
            DispatchMessageW( &message );
        }
        LARGE_INTEGER before{}, after{};
        QueryPerformanceCounter( &before );
        FrameMark;
        QueryPerformanceCounter( &after );
        if( !bridge.Push( { static_cast<uint64_t>( frame ), static_cast<uint64_t>( before.QuadPart ),
            static_cast<uint64_t>( after.QuadPart ) } ) ) return 1;
        for( const auto& swapChain : swapChains )
            if( !Check( swapChain->Present( 1, 0 ), "Present" ) ) return 1;
        if( frame % 30 == 0 ) std::printf( "frame=%d presents=%d\n", frame, frame * 2 );
    }
    for( auto window : windows ) DestroyWindow( window );
    UnregisterClassW( windowClass.lpszClassName, windowClass.hInstance );
    std::printf( "Finished %d frames and %d presents. Bridge drops=%llu.\n", frameCount,
        frameCount * 2, static_cast<unsigned long long>( bridge.Dropped() ) );
}
