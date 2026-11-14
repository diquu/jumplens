#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace jl {

struct LnkInfo {
    uint32_t flags = 0;
    uint32_t fileAttributes = 0;
    double creationTime = 0.0;
    double accessTime = 0.0;
    double writeTime = 0.0;
    uint32_t fileSize = 0;
    uint32_t showCommand = 0;
    uint16_t hotkey = 0;
    std::wstring name;
    std::wstring relativePath;
    std::wstring workingDir;
    std::wstring arguments;
    std::wstring iconLocation;
    std::wstring targetPath;
    std::wstring idListPath;
    std::wstring localBasePath;
    std::wstring commonPathSuffix;
    std::wstring volumeLabel;
    std::wstring networkName;
    std::wstring networkDevice;
    uint32_t driveType = 0;
    uint32_t volumeSerial = 0;
    std::wstring machineId;
    std::wstring volumeId;
    std::wstring fileId;
    std::wstring volumeBirth;
    std::wstring fileBirth;
    std::wstring environmentTarget;
    std::wstring darwinTarget;
    std::wstring shimLayer;
    std::wstring knownFolder;
    std::wstring extraBlocks;
    bool hasPropertyStore = false;
    uint32_t propertyStoreSize = 0;
    std::vector<std::wstring> flagNames;
    std::vector<std::wstring> warnings;
};

bool parse_lnk(const std::vector<uint8_t>& data, UINT codepage, LnkInfo& out, std::wstring& error);
const wchar_t* drive_type_name(uint32_t value);
std::wstring attribute_names(uint32_t attributes);
std::wstring hotkey_text(uint16_t value);
std::wstring show_command_name(uint32_t value);

}
