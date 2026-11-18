#pragma once

#include "tracyx/Event.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tracyx
{

inline constexpr uint32_t SessionVersion = 1;

struct SessionManifest
{
    uint32_t version = SessionVersion;
    ProcessIdentity process;
    uint64_t qpcFrequency = 0;
    uint64_t qpcOrigin = 0;
    uint64_t lostEvents = 0;
    uint64_t queueDrops = 0;
    uint64_t bridgeMissing = 0;
    uint64_t d3d12Events = 0;
    uint64_t dxgkEvents = 0;
    uint64_t d3d12DecodeErrors = 0;
    uint64_t dxgkDecodeErrors = 0;
    uint64_t clockResidualNs = 0;
    uint32_t presentMonState = 0;
    uint64_t presentMonFrames = 0;
    uint64_t presentMonDropped = 0;
    std::string presentMonError;
    std::string tracyFile;
    std::string eventsFile;
    std::string buildSha;
    std::string environment;
    std::string gpuName;
    std::string driverVersion;
    std::string scene;
    std::string captureMode;
};

struct Session
{
    SessionManifest manifest;
    std::vector<Event> events;
};

struct FrameAnchorRecord
{
    uint64_t frameId = 0;
    uint64_t qpcBefore = 0;
    uint64_t qpcAfter = 0;
    uint64_t seriesId = 0;
};

enum class PixCaptureKind : uint8_t
{
    Gpu,
    Timing,
    Existing
};

struct PixCaptureRecord
{
    PixCaptureKind kind = PixCaptureKind::Gpu;
    uint64_t frameId = 0;
    uint64_t seriesId = 0;
    uint64_t qpcBegin = 0;
    uint64_t qpcEnd = 0;
    uint32_t beginResult = 0;
    uint32_t endResult = 0;
    bool started = false;
    bool completed = false;
    std::string fileName;
    std::string error;
};

bool SaveSession( const std::filesystem::path& directory, const Session& session, std::string& error );
bool LoadSession( const std::filesystem::path& directory, Session& session, std::string& error );
bool LoadFrameAnchors( const std::filesystem::path& file, std::vector<FrameAnchorRecord>& anchors, std::string& error );
bool AppendPixCapture( const std::filesystem::path& file, const PixCaptureRecord& capture, std::string& error );
bool LoadPixCaptures( const std::filesystem::path& file, std::vector<PixCaptureRecord>& captures, std::string& error );
std::vector<FrameLink> FindPixCaptureFrames( const PixCaptureRecord& capture,
    const std::vector<FrameAnchorRecord>& anchors );

} // namespace tracyx
