#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jl {

struct DestListHeader {
    uint32_t version = 0;
    uint32_t entryCount = 0;
    uint32_t pinnedCount = 0;
    uint32_t unknown0 = 0;
    uint32_t unknown1 = 0;
    uint32_t unknown2 = 0;
    uint32_t lastIssuedEntryId = 0;
    uint32_t unknown3 = 0;
};

struct DestListEntry {
    int64_t checksum = 0;
    std::wstring volumeDroid;
    std::wstring fileDroid;
    std::wstring volumeBirthDroid;
    std::wstring fileBirthDroid;
    std::wstring hostname;
    int entryNumber = 0;
    int unknown0 = 0;
    float accessCount = 0.0f;
    int pinStatus = 0;
    int interactionCount = 0;
    int unknown3 = 0;
    int unknown4 = 0;
    int mruPosition = 0;
    double lastModified = 0.0;
    double droidTime = 0.0;
    std::wstring macAddress;
    std::wstring path;
    std::wstring pathResolved;
    int propertyStoreSize = 0;
    std::wstring propertyStoreHex;
    std::wstring notes;
};

struct DestList {
    DestListHeader header;
    std::vector<DestListEntry> entries;
    std::vector<std::wstring> warnings;
};

bool parse_destlist(const std::vector<uint8_t>& data, DestList& out, std::wstring& error);
std::wstring resolve_known_folder(const std::wstring& guid, bool& found);
std::wstring resolve_shell_path(const std::wstring& path, std::wstring* resolved);
bool droid_time(const std::wstring& droid, double& unix_seconds, std::wstring& mac);

}
