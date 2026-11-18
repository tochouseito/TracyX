#include <tracy/Tracy.hpp>
#include "tracyx/DredBridge.hpp"
#include "tracyx/FrameBridge.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dstorage.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace
{

constexpr size_t InputBytes = 17'040;

struct HandleCloser
{
    void operator()( void* handle ) const
    {
        if( handle ) CloseHandle( handle );
    }
};

bool Check( HRESULT result, const char* operation )
{
    if( SUCCEEDED( result ) ) return true;
    std::fprintf( stderr, "%s failed: 0x%08x\n", operation, static_cast<unsigned>( result ) );
    return false;
}

bool MarkFrame( tracyx::FrameBridge& bridge, uint64_t frame )
{
    LARGE_INTEGER before{}, after{};
    if( !QueryPerformanceCounter( &before ) ) return false;
    FrameMark;
    if( !QueryPerformanceCounter( &after ) ) return false;
    return bridge.Push( { frame, static_cast<uint64_t>( before.QuadPart ), static_cast<uint64_t>( after.QuadPart ) } );
}

} // namespace

int wmain( int argc, wchar_t** argv )
{
    if( argc < 2 || argc > 4 )
    {
        std::fwprintf( stderr, L"Usage: TracyXProbeDirectStorage <session-directory> [frames] [remove]\n" );
        return 2;
    }
    const int frames = argc >= 3 ? _wtoi( argv[2] ) : 600;
    const bool removeDevice = argc == 4 && wcscmp( argv[3], L"remove" ) == 0;
    if( frames < 2 || frames > 10'000 || ( argc == 4 && !removeDevice ) ) return 2;

    const auto sessionDirectory = std::filesystem::absolute( std::filesystem::path( argv[1] ) );
    std::error_code fileError;
    std::filesystem::create_directories( sessionDirectory, fileError );
    if( fileError ) return 1;
    const auto inputFile = sessionDirectory / "directstorage-input.bin";
    std::array<uint8_t, InputBytes> input{};
    for( size_t index = 0; index < input.size(); ++index ) input[index] = static_cast<uint8_t>( index );
    {
        std::ofstream output( inputFile, std::ios::binary | std::ios::trunc );
        output.write( reinterpret_cast<const char*>( input.data() ), input.size() );
        if( !output ) return 1;
    }
    tracyx::FrameBridge bridge( sessionDirectory / "frame-anchors.jsonl" );
    if( !bridge.Healthy() ) return 1;
    std::printf( "TracyX DirectStorage probe PID=%lu frames=%d\n", GetCurrentProcessId(), frames );
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
    if( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &dredSettings ) ) ) )
    {
        dredSettings->SetAutoBreadcrumbsEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
        dredSettings->SetPageFaultEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
    }

    ComPtr<ID3D12Device> device;
    if( !Check( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS( &device ) ), "D3D12CreateDevice" ) ) return 1;
    ComPtr<IDStorageFactory> factory;
    if( !Check( DStorageGetFactory( IID_PPV_ARGS( &factory ) ), "DStorageGetFactory" ) ) return 1;
    ComPtr<IDStorageFile> file;
    if( !Check( factory->OpenFile( inputFile.c_str(), IID_PPV_ARGS( &file ) ), "OpenFile" ) ) return 1;

    DSTORAGE_QUEUE_DESC queueDescription{};
    queueDescription.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    queueDescription.Priority = DSTORAGE_PRIORITY_NORMAL;
    queueDescription.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    queueDescription.Device = device.Get();
    ComPtr<IDStorageQueue> queue;
    if( !Check( factory->CreateQueue( &queueDescription, IID_PPV_ARGS( &queue ) ), "CreateQueue" ) ) return 1;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = InputBytes;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.Format = DXGI_FORMAT_UNKNOWN;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    if( !Check( device->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS( &resource ) ), "CreateCommittedResource" ) ) return 1;

    DSTORAGE_REQUEST request{};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_BUFFER;
    request.Source.File.Source = file.Get();
    request.Source.File.Offset = 0;
    request.Source.File.Size = InputBytes;
    request.UncompressedSize = InputBytes;
    request.Destination.Buffer.Resource = resource.Get();
    request.Destination.Buffer.Offset = 0;
    request.Destination.Buffer.Size = InputBytes;

    ComPtr<ID3D12Fence> fence;
    if( !Check( device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &fence ) ), "CreateFence" ) ) return 1;
    std::unique_ptr<void, HandleCloser> completion( CreateEventW( nullptr, FALSE, FALSE, nullptr ) );
    if( !completion ) return 1;
    for( int frame = 1; frame <= frames; ++frame )
    {
        queue->EnqueueRequest( &request );
        if( !Check( fence->SetEventOnCompletion( frame, completion.get() ), "SetEventOnCompletion" ) ) return 1;
        queue->EnqueueSignal( fence.Get(), frame );
        queue->Submit();
        if( WaitForSingleObject( completion.get(), 10'000 ) != WAIT_OBJECT_0 ) return 1;
        if( !MarkFrame( bridge, static_cast<uint64_t>( frame ) ) ) return 1;
        std::this_thread::sleep_for( std::chrono::milliseconds( 16 ) );
    }

    DSTORAGE_ERROR_RECORD storageError{};
    queue->RetrieveErrorRecord( &storageError );
    if( !Check( storageError.FirstFailure.HResult, "DirectStorage request" ) ) return 1;
    if( removeDevice )
    {
        ComPtr<ID3D12Device5> removable;
        if( !Check( device.As( &removable ), "ID3D12Device5" ) ) return 1;
        removable->RemoveDevice();
        const auto reason = device->GetDeviceRemovedReason();
        if( SUCCEEDED( reason ) || !tracyx::CaptureDred( device.Get(), sessionDirectory ) ) return 1;
        if( !MarkFrame( bridge, static_cast<uint64_t>( frames ) + 1 ) ) return 1;
        std::printf( "Removed test device: 0x%08x\n", static_cast<unsigned>( reason ) );
    }
    std::printf( "Completed %d DirectStorage reads. Bridge drops=%llu.\n", frames,
        static_cast<unsigned long long>( bridge.Dropped() ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    return 0;
}
