#pragma once

#include <string>
#include <vector>

namespace jl {

struct Entry {
    int id = 0;
    int sourceIndex = 0;
    bool automatic = true;

    std::wstring appId;
    std::wstring appName;

    int entryNumber = 0;
    int mruPosition = 0;
    int interactions = 0;
    bool pinned = false;
    bool hasLnk = false;
    bool orphanStream = false;

    double lastModified = 0.0;
    double entryCreated = 0.0;
    double targetCreated = 0.0;
    double targetModified = 0.0;
    double targetAccessed = 0.0;

    int categoryType = 0;
    std::wstring category;
    double sortTime = 0.0;

    std::wstring hostname;
    std::wstring macAddress;
    std::wstring objectPath;
    std::wstring objectPathResolved;
    std::wstring target;
    std::wstring arguments;
    std::wstring workingDir;

    uint64_t fileSize = 0;
    std::wstring attributes;
    std::wstring showCommand;
    std::wstring hotkey;
    std::wstring iconLocation;
    std::wstring driveType;
    std::wstring volumeSerial;
    std::wstring volumeLabel;
    std::wstring networkName;

    std::wstring machineId;
    std::wstring trackerVolumeId;
    std::wstring trackerFileId;
    std::wstring trackerVolumeBirth;
    std::wstring trackerFileBirth;
    std::wstring idListPath;
    std::wstring environmentTarget;
    std::wstring shimLayer;
    std::wstring knownFolder;
    std::wstring extraBlocks;
    std::wstring relativePath;
    std::wstring linkFlags;
    std::wstring notes;
    std::wstring checksum;
    std::wstring propertyStore;

    std::wstring search;
};

struct SourceInfo {
    std::wstring path;
    std::wstring name;
    std::wstring appId;
    std::wstring appName;
    bool automatic = true;
    bool parsed = false;
    bool temporary = false;
    uint64_t size = 0;
    int entryCount = 0;
    int destListVersion = 0;
    int pinnedCount = 0;
    int categories = 0;
    std::wstring notes;
};

struct Dataset {
    std::vector<Entry> entries;
    std::vector<SourceInfo> sources;
    int filesScanned = 0;
    int filesParsed = 0;
    double seconds = 0.0;
    double scanSeconds = 0.0;
    int hosts = 0;
    int pinned = 0;
    std::wstring rangeFrom;
    std::wstring rangeTo;
    std::vector<std::wstring> warnings;
};

}
