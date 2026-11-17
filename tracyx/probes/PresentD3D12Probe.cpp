#include <tracy/Tracy.hpp>
#include "tracyx/FrameBridge.hpp"
#include "tracyx/Session.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#ifdef TRACYX_HAS_PIX
#include <WinPixEventRuntime/pix3.h>
#endif

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

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

uint64_t NowQpc()
{
    LARGE_INTEGER counter{};
    QueryPerformanceCounter( &counter );
    return static_cast<uint64_t>( counter.QuadPart );
}

std::string HresultError( const char* operation, HRESULT result )
{
    char message[128]{};
    std::snprintf( message, sizeof( message ), "%s failed: 0x%08X", operation, static_cast<unsigned>( result ) );
    return message;
}

bool IsElevated()
{
    HANDLE token = nullptr;
    if( !OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ) ) return false;
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    const auto result = GetTokenInformation( token, TokenElevation, &elevation, sizeof( elevation ), &bytes );
    CloseHandle( token );
    return result && elevation.TokenIsElevated;
}

} // namespace

int main( int argc, char** argv )
{
    const int frameCount = argc > 1 ? std::atoi( argv[1] ) : 180;
    if( frameCount <= 0 || frameCount > 10'000 ) return 2;
    const auto directory = std::filesystem::absolute( argc > 2 ? std::filesystem::path( argv[2] ) : std::filesystem::current_path() );
    std::filesystem::create_directories( directory );
    const std::string pixMode = argc > 3 ? argv[3] : "";
    const auto captureStartFrame = argc > 4 ? std::atoi( argv[4] ) : std::min( 10, frameCount );
    if( argc > 5 || ( !pixMode.empty() && pixMode != "gpu" && pixMode != "timing" ) ||
        ( argc > 4 && ( pixMode.empty() || captureStartFrame < 1 || captureStartFrame > frameCount ) ) )
    {
        std::fprintf( stderr, "Usage: TracyXProbePresentD3D12 [frames] [directory] [gpu|timing] [capture-start-frame]\n" );
        return 2;
    }
    tracyx::PixCaptureRecord pixCapture;
    pixCapture.kind = pixMode == "timing" ? tracyx::PixCaptureKind::Timing : tracyx::PixCaptureKind::Gpu;
    pixCapture.fileName = "pix-" + ( pixMode == "timing" ? std::string( "timing" ) : std::string( "gpu" ) ) +
        "-" + std::to_string( GetCurrentProcessId() ) + "-" + std::to_string( NowQpc() ) + ".wpix";
    const auto pixFile = directory / pixCapture.fileName;
    HMODULE capturer = nullptr;
    if( !pixMode.empty() )
    {
#ifdef TRACYX_HAS_PIX
        if( pixMode == "timing" && !IsElevated() )
            pixCapture.error = "PIX Timing capture requires an elevated process; restart as administrator";
        else
        {
            // GPU Capturer は D3D12 API の最初の呼び出しより前にロードする。
            capturer = pixMode == "gpu" ? PIXLoadLatestWinPixGpuCapturerLibrary() : PIXLoadLatestWinPixTimingCapturerLibrary();
            if( !capturer )
                pixCapture.error = "PIX Capturer DLL unavailable; install PIX and restart before D3D12 device creation (Win32 " +
                    std::to_string( GetLastError() ) + ")";
        }
#else
        pixCapture.error = "WinPixEventRuntime SDK unavailable; configure TRACYX_WINPIX_ROOT and rebuild";
#endif
        pixCapture.qpcBegin = NowQpc();
        pixCapture.qpcEnd = pixCapture.qpcBegin;
        if( !pixCapture.error.empty() ) std::fprintf( stderr, "%s\n", pixCapture.error.c_str() );
    }
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
#ifdef TRACYX_HAS_PIX
        const auto beginFrame = captureStartFrame;
        const auto endFrame = std::min( beginFrame + 10, frameCount );
        if( capturer && frame == beginFrame )
        {
            pixCapture.frameId = static_cast<uint64_t>( frame );
            if( pixMode == "gpu" )
            {
                const auto target = PIXSetTargetWindow( windows[0] );
                if( FAILED( target ) ) pixCapture.error = HresultError( "PIXSetTargetWindow", target );
            }
            if( pixCapture.error.empty() )
            {
                PIXCaptureParameters parameters{};
                const auto fileName = pixFile.wstring();
                if( pixMode == "gpu" ) parameters.GpuCaptureParameters.FileName = fileName.c_str();
                else
                {
                    parameters.TimingCaptureParameters.FileName = fileName.c_str();
                    parameters.TimingCaptureParameters.CaptureGpuTiming = TRUE;
                    parameters.TimingCaptureParameters.CaptureCallstacks = TRUE;
                    parameters.TimingCaptureParameters.CaptureCpuSamples = TRUE;
                    parameters.TimingCaptureParameters.CpuSamplesPerSecond = 1000;
                }
                pixCapture.qpcBegin = NowQpc();
                const auto result = PIXBeginCapture( pixMode == "gpu" ? PIX_CAPTURE_GPU : PIX_CAPTURE_TIMING, &parameters );
                pixCapture.beginResult = static_cast<uint32_t>( result );
                pixCapture.started = SUCCEEDED( result );
                if( !pixCapture.started ) pixCapture.error = HresultError( "PIXBeginCapture", result );
            }
        }
#endif
        for( const auto& swapChain : swapChains )
            if( !Check( swapChain->Present( 1, 0 ), "Present" ) ) return 1;
#ifdef TRACYX_HAS_PIX
        if( pixCapture.started && frame == endFrame )
        {
            const auto result = PIXEndCapture( FALSE );
            pixCapture.qpcEnd = NowQpc();
            pixCapture.endResult = static_cast<uint32_t>( result );
            pixCapture.completed = SUCCEEDED( result );
            if( !pixCapture.completed ) pixCapture.error = HresultError( "PIXEndCapture", result );
        }
#endif
        if( frame % 30 == 0 ) std::printf( "frame=%d presents=%d\n", frame, frame * 2 );
    }
    if( !pixMode.empty() )
    {
        if( pixCapture.qpcEnd < pixCapture.qpcBegin ) pixCapture.qpcEnd = NowQpc();
        if( pixCapture.completed )
        {
            for( int attempt = 0; attempt < 50 && !std::filesystem::is_regular_file( pixFile ); ++attempt )
                std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            if( !std::filesystem::is_regular_file( pixFile ) )
            {
                pixCapture.completed = false;
                pixCapture.error = "PIXEndCapture succeeded but the .wpix file was not created";
            }
        }
        std::string error;
        if( !tracyx::AppendPixCapture( directory / "pix-captures.jsonl", pixCapture, error ) )
            std::fprintf( stderr, "PIX capture index: %s\n", error.c_str() );
        std::printf( "PIX %s capture: %s%s%s%s\n", pixMode.c_str(),
            pixCapture.completed ? "saved " : "unavailable", pixCapture.completed ? pixFile.string().c_str() : "",
            pixCapture.error.empty() ? "" : " - ", pixCapture.error.c_str() );
    }
    for( auto window : windows ) DestroyWindow( window );
    UnregisterClassW( windowClass.lpszClassName, windowClass.hInstance );
    std::printf( "Finished %d frames and %d presents. Bridge drops=%llu.\n", frameCount,
        frameCount * 2, static_cast<unsigned long long>( bridge.Dropped() ) );
}
