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
    std::string tracyFile;
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

bool SaveSession( const std::filesystem::path& directory, const Session& session, std::string& error );
bool LoadSession( const std::filesystem::path& directory, Session& session, std::string& error );
bool LoadFrameAnchors( const std::filesystem::path& file, std::vector<FrameAnchorRecord>& anchors, std::string& error );

} // namespace tracyx
