cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(TracyXIntegration LANGUAGES CXX)
    enable_testing()
endif()

add_library(TracyXCore
    src/Clock.cpp
    src/Correlation.cpp
    src/Diagnostics.cpp
    src/FrameBridge.cpp
    src/Session.cpp
)
target_compile_features(TracyXCore PUBLIC cxx_std_20)
target_include_directories(TracyXCore PUBLIC include)
target_compile_options(TracyXCore PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

add_executable(TracyXCoreTests tests/CoreTests.cpp)
target_link_libraries(TracyXCoreTests PRIVATE TracyXCore)
add_test(NAME TracyXCoreTests COMMAND TracyXCoreTests)

option(TRACYX_BUILD_PROBES "Build M0 profiling targets" ON)
if(WIN32 AND TRACYX_BUILD_PROBES)
    add_library(TracyXProbeClient STATIC ../public/TracyClient.cpp)
    target_compile_features(TracyXProbeClient PUBLIC cxx_std_20)
    target_include_directories(TracyXProbeClient PUBLIC ../public)
    target_compile_definitions(TracyXProbeClient PUBLIC TRACY_ENABLE)
    target_compile_options(TracyXProbeClient PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_link_libraries(TracyXProbeClient PUBLIC ws2_32 dbghelp secur32)

    add_executable(TracyXProbeCpp probes/CppProbe.cpp)
    target_link_libraries(TracyXProbeCpp PRIVATE TracyXProbeClient)

    add_executable(TracyXProbeD3D12 probes/D3D12Probe.cpp)
    target_link_libraries(TracyXProbeD3D12 PRIVATE TracyXProbeClient TracyXCore d3d12 dxgi)
    add_executable(TracyXProbePresentD3D12 probes/PresentD3D12Probe.cpp)
    target_link_libraries(TracyXProbePresentD3D12 PRIVATE TracyXProbeClient TracyXCore d3d12 dxgi advapi32)
    set(TRACYX_WINPIX_ROOT "$ENV{USERPROFILE}/.nuget/packages/winpixeventruntime/1.0.240308001" CACHE PATH "Optional WinPixEventRuntime NuGet package")
    if(EXISTS "${TRACYX_WINPIX_ROOT}/Include/WinPixEventRuntime/pix3.h" AND
       EXISTS "${TRACYX_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.lib")
        foreach(probe TracyXProbeD3D12 TracyXProbePresentD3D12)
            target_include_directories(${probe} PRIVATE "${TRACYX_WINPIX_ROOT}/Include")
            target_link_libraries(${probe} PRIVATE "${TRACYX_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.lib")
            target_compile_definitions(${probe} PRIVATE TRACYX_HAS_PIX)
            add_custom_command(TARGET ${probe} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${TRACYX_WINPIX_ROOT}/bin/x64/WinPixEventRuntime.dll"
                    "$<TARGET_FILE_DIR:${probe}>/WinPixEventRuntime.dll")
        endforeach()
    endif()
endif()

set(TRACYX_DX_TIMING_ROOT "" CACHE PATH "Pinned DxTimingCaptureLibrary source tree")
set(TRACYX_D3D12_AGILITY_INCLUDE "" CACHE PATH "D3D12 Agility SDK include directory")
set(TRACYX_PRESENTMON_SDK_ROOT "" CACHE PATH "Optional installed PresentMon SDK directory")
if(WIN32 AND TRACYX_PRESENTMON_SDK_ROOT)
    if(NOT EXISTS "${TRACYX_PRESENTMON_SDK_ROOT}/PresentMonAPI.h" OR
       NOT EXISTS "${TRACYX_PRESENTMON_SDK_ROOT}/PresentMonAPI2Loader.lib" OR
       NOT EXISTS "${TRACYX_PRESENTMON_SDK_ROOT}/PresentMonAPI2Loader.dll")
        message(FATAL_ERROR "PresentMon SDK header, loader library or DLL is missing")
    endif()
    add_executable(TracyXProbePresentMon probes/PresentMonSdkProbe.cpp)
    target_compile_features(TracyXProbePresentMon PRIVATE cxx_std_20)
    target_include_directories(TracyXProbePresentMon PRIVATE "${TRACYX_PRESENTMON_SDK_ROOT}")
    target_link_libraries(TracyXProbePresentMon PRIVATE "${TRACYX_PRESENTMON_SDK_ROOT}/PresentMonAPI2Loader.lib")
    add_custom_command(TARGET TracyXProbePresentMon POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TRACYX_PRESENTMON_SDK_ROOT}/PresentMonAPI2Loader.dll"
            "$<TARGET_FILE_DIR:TracyXProbePresentMon>/PresentMonAPI2Loader.dll")
endif()
if(WIN32 AND TRACYX_DX_TIMING_ROOT)
    if(NOT TRACYX_D3D12_AGILITY_INCLUDE)
        set(TRACYX_D3D12_AGILITY_INCLUDE "$ENV{USERPROFILE}/.nuget/packages/microsoft.direct3d.d3d12/1.619.5/build/native/include")
    endif()
    add_library(TracyXDxTiming STATIC IMPORTED)
    set_target_properties(TracyXDxTiming PROPERTIES
        IMPORTED_LOCATION_DEBUG "${TRACYX_DX_TIMING_ROOT}/lib/x64/Debug/lib/DxTimingCaptureLibrary.lib"
        IMPORTED_LOCATION_RELEASE "${TRACYX_DX_TIMING_ROOT}/lib/x64/Release/lib/DxTimingCaptureLibrary.lib"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )
    add_executable(TracyXAgent agent/Agent.cpp)
    target_compile_features(TracyXAgent PRIVATE cxx_std_20)
    target_include_directories(TracyXAgent PRIVATE
        "${TRACYX_DX_TIMING_ROOT}/include"
        "${TRACYX_DX_TIMING_ROOT}/third_party/PixEventDecoder/include"
        "${TRACYX_DX_TIMING_ROOT}/third_party/dxgk"
        "${TRACYX_DX_TIMING_ROOT}/third_party/directstorage"
        "${TRACYX_D3D12_AGILITY_INCLUDE}"
    )
    target_compile_definitions(TracyXAgent PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(TracyXAgent PRIVATE TracyXCore TracyXDxTiming advapi32 tdh d3d12 dxgi uuid onecore)
    if(TRACYX_PRESENTMON_SDK_ROOT)
        target_sources(TracyXAgent PRIVATE agent/PresentMonAdapter.cpp)
        target_include_directories(TracyXAgent PRIVATE "${TRACYX_PRESENTMON_SDK_ROOT}")
        target_link_libraries(TracyXAgent PRIVATE "${TRACYX_PRESENTMON_SDK_ROOT}/PresentMonAPI2Loader.lib")
        target_compile_definitions(TracyXAgent PRIVATE TRACYX_HAS_PRESENTMON)
        add_custom_command(TARGET TracyXAgent POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TRACYX_PRESENTMON_SDK_ROOT}/PresentMonAPI2Loader.dll"
                "$<TARGET_FILE_DIR:TracyXAgent>/PresentMonAPI2Loader.dll")
    endif()
endif()
