#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <filesystem>
#include <fstream>

namespace tracyx
{

// Device removal を検出した対象プロセス内で呼ぶ。DRED が無効でも reason は記録する。
inline bool CaptureDred( ID3D12Device* device, const std::filesystem::path& sessionDirectory )
{
    if( !device ) return false;
    const auto reason = device->GetDeviceRemovedReason();
    if( SUCCEEDED( reason ) ) return false;

    LARGE_INTEGER timestamp{};
    if( !QueryPerformanceCounter( &timestamp ) ) return false;
    uint64_t pageFaultAddress = 0;
    uint32_t breadcrumbCount = 0;
    ID3D12DeviceRemovedExtendedData* dred = nullptr;
    if( SUCCEEDED( device->QueryInterface( __uuidof( ID3D12DeviceRemovedExtendedData ),
        reinterpret_cast<void**>( &dred ) ) ) )
    {
        D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
        if( SUCCEEDED( dred->GetPageFaultAllocationOutput( &pageFault ) ) )
            pageFaultAddress = pageFault.PageFaultVA;
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
        if( SUCCEEDED( dred->GetAutoBreadcrumbsOutput( &breadcrumbs ) ) )
        {
            for( auto* node = breadcrumbs.pHeadAutoBreadcrumbNode; node && breadcrumbCount < 4096;
                node = node->pNext ) ++breadcrumbCount;
        }
        dred->Release();
    }

    std::error_code error;
    std::filesystem::create_directories( sessionDirectory, error );
    if( error ) return false;
    std::ofstream output( sessionDirectory / "dred.txt", std::ios::binary | std::ios::trunc );
    output << timestamp.QuadPart << ' ' << static_cast<uint32_t>( reason ) << ' '
        << pageFaultAddress << ' ' << breadcrumbCount << '\n';
    return static_cast<bool>( output );
}

} // namespace tracyx
