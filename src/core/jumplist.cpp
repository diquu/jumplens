#include "jumplist.h"

#include "appids.h"
#include "bytes.h"
#include "cfb.h"
#include "destlist.h"
#include "lnk.h"
#include "text.h"

#include <algorithm>

namespace jl {
namespace {

const uint8_t kCfbMagic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
const uint8_t kLnkSignature[20] = {0x4C, 0x00, 0x00, 0x00, 0x01, 0x14, 0x02, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
const uint8_t kFooter[4] = {0xAB, 0xFB, 0xBF, 0xBA};

std::wstring hex_upper(int value) {
    wchar_t buffer[16];
    swprintf_s(buffer, L"%X", static_cast<unsigned int>(value));
    return buffer;
}

std::wstring app_id_from_file(const std::wstring& path) {
    std::wstring name = file_name_of(path);
    size_t cut = name.find(L'.');
    if (cut == std::wstring::npos) {
        return name;
    }
    return name.substr(0, cut);
}

void append(std::vector<std::wstring>& parts, const std::wstring& value) {
    if (!value.empty()) {
        parts.push_back(value);
    }
}

void build_search(Entry& e) {
    std::vector<std::wstring> parts;
    append(parts, e.appName);
    append(parts, e.appId);
    append(parts, e.target);
    append(parts, e.arguments);
    append(parts, e.workingDir);
    append(parts, e.objectPath);
    append(parts, e.objectPathResolved);
    append(parts, e.hostname);
    append(parts, e.category);
    append(parts, e.idListPath);
    append(parts, e.machineId);
    append(parts, e.trackerVolumeId);
    append(parts, e.trackerFileId);
    append(parts, e.notes);
    e.search = lower(join(parts, L" "));
}

void fill_from_lnk(Entry& e, const LnkInfo& lnk) {
    e.hasLnk = true;
    e.arguments = lnk.arguments;
    e.workingDir = lnk.workingDir;
    e.fileSize = lnk.fileSize;
    e.attributes = attribute_names(lnk.fileAttributes);
    e.showCommand = show_command_name(lnk.showCommand);
    e.hotkey = hotkey_text(lnk.hotkey);
    e.iconLocation = lnk.iconLocation;
    e.driveType = drive_type_name(lnk.driveType);
    if (lnk.volumeSerial != 0) {
        e.volumeSerial = format_hex(lnk.volumeSerial, 8);
    }
    e.volumeLabel = lnk.volumeLabel;
    e.networkName = lnk.networkName;
    if (!lnk.networkDevice.empty()) {
        e.networkName = lnk.networkDevice;
    }
    e.machineId = lnk.machineId;
    e.trackerVolumeId = lnk.volumeId;
    e.trackerFileId = lnk.fileId;
    e.trackerVolumeBirth = lnk.volumeBirth;
    e.trackerFileBirth = lnk.fileBirth;
    e.idListPath = lnk.idListPath;
    e.environmentTarget = lnk.environmentTarget;
    e.shimLayer = lnk.shimLayer;
    e.knownFolder = lnk.knownFolder;
    e.extraBlocks = lnk.extraBlocks;
    e.relativePath = lnk.relativePath;
    e.linkFlags = join(lnk.flagNames, L", ");
    e.targetCreated = lnk.creationTime;
    e.targetModified = lnk.writeTime;
    e.targetAccessed = lnk.accessTime;

    if (!lnk.targetPath.empty()) {
        e.target = lnk.targetPath;
    } else if (!lnk.idListPath.empty()) {
        e.target = lnk.idListPath;
    } else if (!lnk.darwinTarget.empty()) {
        e.target = lnk.darwinTarget;
    } else if (!lnk.environmentTarget.empty()) {
        e.target = lnk.environmentTarget;
    }
    std::vector<std::wstring> notes;
    for (const std::wstring& warning : lnk.warnings) {
        notes.push_back(warning);
    }
    if (lnk.hasPropertyStore) {
        wchar_t buffer[96];
        swprintf_s(buffer, L"link carries a %u byte property store", lnk.propertyStoreSize);
        notes.push_back(buffer);
    }
    if (!notes.empty()) {
        e.notes = join(notes, L"; ");
    }
}

void fill_from_destlist(Entry& e, const DestListEntry& dl) {
    e.hostname = dl.hostname;
    e.macAddress = dl.macAddress;
    e.entryCreated = dl.droidTime;
    e.objectPath = dl.path;
    e.objectPathResolved = dl.pathResolved;
    e.entryNumber = dl.entryNumber;
    e.mruPosition = dl.mruPosition;
    e.interactions = dl.interactionCount > 0 ? dl.interactionCount : static_cast<int>(dl.accessCount);
    e.pinned = dl.pinStatus != -1;
    e.lastModified = dl.lastModified;
    e.checksum = format_hex(static_cast<uint64_t>(dl.checksum), 16);
    if (dl.propertyStoreSize > 0) {
        e.propertyStore = format_int(dl.propertyStoreSize) + L" bytes";
    }
    if (!dl.notes.empty()) {
        e.notes = dl.notes;
    }
}

SourceInfo base_source(const std::wstring& path, uint64_t size) {
    SourceInfo info;
    info.path = path;
    info.name = file_name_of(path);
    info.appId = app_id_from_file(path);
    info.appName = app_id_label(info.appId);
    info.size = size;
    std::wstring name = lower(info.name);
    info.temporary = name.rfind(L"~rf", 0) == 0 || name.find(L".tmp") != std::wstring::npos;
    return info;
}

bool parse_automatic(const std::vector<uint8_t>& data, const std::wstring& path, SourceInfo& info,
                     std::vector<Entry>& out, std::wstring& error) {
    CompoundFile cfb;
    if (!cfb.open(data, error)) {
        return false;
    }
    info.automatic = true;
    std::vector<std::wstring> notes;
    const CfbDirEntry* destStream = cfb.find(L"DestList");
    if (destStream == nullptr) {
        info.notes = L"the compound file has no DestList stream, so the jump list holds no entries";
        return true;
    }
    std::vector<uint8_t> destBytes;
    if (!cfb.read(*destStream, destBytes)) {
        error = L"the DestList stream could not be read";
        return false;
    }
    if (destBytes.size() < 32) {
        info.destListVersion = 0;
        info.notes = L"the DestList stream is empty, so the jump list holds no entries";
        return true;
    }
    DestList list;
    if (!parse_destlist(destBytes, list, error)) {
        return false;
    }
    info.destListVersion = static_cast<int>(list.header.version);
    info.entryCount = static_cast<int>(list.entries.size());
    info.pinnedCount = 0;
    for (const std::wstring& warning : list.warnings) {
        notes.push_back(warning);
    }

    std::vector<const CfbDirEntry*> streams;
    for (const CfbDirEntry& entry : cfb.entries()) {
        if (entry.kind != 2) {
            continue;
        }
        if (iequals(entry.name, L"DestList") || iequals(entry.name, L"DestListPropertyStore")) {
            continue;
        }
        streams.push_back(&entry);
    }

    std::vector<bool> used(streams.size(), false);
    for (const DestListEntry& dl : list.entries) {
        Entry e;
        e.automatic = true;
        e.appId = info.appId;
        e.appName = info.appName;
        fill_from_destlist(e, dl);
        e.category = L"Automatic destinations";
        e.categoryType = 0;
        if (!dl.pathResolved.empty()) {
            e.target = dl.pathResolved;
        } else {
            e.target = dl.path;
        }
        std::wstring streamName = hex_upper(dl.entryNumber);
        for (size_t i = 0; i < streams.size(); ++i) {
            if (iequals(streams[i]->name, streamName)) {
                used[i] = true;
                std::vector<uint8_t> linkBytes;
                if (cfb.read(*streams[i], linkBytes) && linkBytes.size() > 4) {
                    LnkInfo lnk;
                    std::wstring linkError;
                    if (parse_lnk(linkBytes, 1252, lnk, linkError)) {
                        std::wstring destNotes = e.notes;
                        fill_from_lnk(e, lnk);
                        if (!destNotes.empty()) {
                            e.notes = destNotes + L"; " + e.notes;
                        }
                        if (!lnk.targetPath.empty()) {
                            e.target = lnk.targetPath;
                        }
                    } else {
                        if (!e.notes.empty()) {
                            e.notes += L"; ";
                        }
                        e.notes += L"link stream could not be read: " + linkError;
                    }
                }
                break;
            }
        }
        if (e.pinned) {
            ++info.pinnedCount;
        }
        build_search(e);
        out.push_back(std::move(e));
    }

    for (size_t i = 0; i < streams.size(); ++i) {
        if (used[i]) {
            continue;
        }
        std::vector<uint8_t> linkBytes;
        if (!cfb.read(*streams[i], linkBytes) || linkBytes.size() <= 4) {
            continue;
        }
        if (linkBytes[0] != 0x4C) {
            continue;
        }
        Entry e;
        e.automatic = true;
        e.orphanStream = true;
        e.appId = info.appId;
        e.appName = info.appName;
        e.category = L"Stream without a DestList entry";
        LnkInfo lnk;
        std::wstring linkError;
        if (parse_lnk(linkBytes, 1252, lnk, linkError)) {
            fill_from_lnk(e, lnk);
            e.notes = L"the compound file holds this link stream but no DestList entry points at it";
        } else {
            e.notes = L"unreadable link stream: " + linkError;
        }
        e.lastModified = e.targetModified > 0.0 ? e.targetModified : e.targetCreated;
        e.sortTime = e.lastModified;
        build_search(e);
        out.push_back(std::move(e));
    }

    if (!notes.empty()) {
        info.notes = join(notes, L"; ");
    }
    return true;
}

int find_bytes(const uint8_t* data, size_t size, const uint8_t* needle, size_t needleSize, size_t start) {
    if (needleSize == 0 || size < needleSize) {
        return -1;
    }
    for (size_t i = start; i + needleSize <= size; ++i) {
        if (memcmp(data + i, needle, needleSize) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void parse_custom(const std::vector<uint8_t>& data, const std::wstring& path, SourceInfo& info, std::vector<Entry>& out,
                  std::wstring& error) {
    info.automatic = false;
    if (data.size() < 24) {
        error = L"the custom destinations file is smaller than its header";
        return;
    }
    uint32_t fileVersion = rd_u32(data.data());
    int categories = static_cast<int>(rd_u32(data.data() + 4));
    uint32_t footerCheck = rd_u32(data.data() + data.size() - 4);
    if (footerCheck != 0xBABFFBABu) {
        error = L"the custom destinations footer is missing";
        return;
    }
    std::vector<std::wstring> notes;
    if (fileVersion != 2) {
        wchar_t buffer[96];
        swprintf_s(buffer, L"unexpected format version %u", fileVersion);
        notes.push_back(buffer);
    }

    std::vector<int> footerOffsets;
    int cursor = 0;
    while (cursor < static_cast<int>(data.size())) {
        int hit = find_bytes(data.data(), data.size(), kFooter, 4, static_cast<size_t>(cursor));
        if (hit < 0) {
            break;
        }
        footerOffsets.push_back(hit);
        cursor = hit + 4;
    }
    if (footerOffsets.empty()) {
        error = L"the custom destinations file has no category footer";
        return;
    }

    int chunkStart = 0;
    int categoryIndex = 0;
    for (int footerOffset : footerOffsets) {
        int chunkSize = footerOffset - chunkStart + 4;
        if (chunkSize < 16 || chunkStart + chunkSize > static_cast<int>(data.size())) {
            chunkStart += chunkSize;
            continue;
        }
        const uint8_t* chunk = data.data() + chunkStart;
        size_t chunkLen = static_cast<size_t>(chunkSize);
        int categoryOffset = chunkStart == 0 ? 12 : 0;
        if (static_cast<size_t>(categoryOffset) + 4 > chunkLen) {
            chunkStart += chunkSize;
            continue;
        }
        int type = rd_i32(chunk + categoryOffset);
        size_t fieldOffset = static_cast<size_t>(categoryOffset) + 4;
        std::wstring categoryName;
        int knownId = 0;
        int declared = 0;
        if (type == 0 && chunkLen >= fieldOffset + 2) {
            int nameLen = rd_i16(chunk + fieldOffset);
            size_t available = (chunkLen - fieldOffset - 2) / 2;
            if (nameLen < 0 || static_cast<size_t>(nameLen) > available) {
                nameLen = static_cast<int>(std::min<size_t>(static_cast<size_t>(std::max(nameLen, 0)), available));
            }
            categoryName = from_utf16(chunk + fieldOffset + 2, static_cast<size_t>(nameLen) * 2);
            fieldOffset += 2 + static_cast<size_t>(nameLen) * 2;
            if (chunkLen >= fieldOffset + 4) {
                declared = rd_i32(chunk + fieldOffset);
            }
        } else if (type == 1 && chunkLen >= fieldOffset + 4) {
            knownId = rd_i32(chunk + fieldOffset);
        } else if (type == 2 && chunkLen >= fieldOffset + 4) {
            declared = rd_i32(chunk + fieldOffset);
        }

        std::wstring categoryLabel;
        (void)declared;
        if (type == 0) {
            categoryLabel = categoryName.empty() ? L"Custom category" : categoryName;
        } else if (type == 1) {
            categoryLabel = knownId == 1 ? L"Frequent destinations" : (knownId == 2 ? L"Recent destinations" : L"Known category " + format_int(knownId));
        } else if (type == 2) {
            categoryLabel = L"Tasks";
        } else {
            categoryLabel = L"Category type " + format_int(type);
        }

        std::vector<int> linkOffsets;
        int scan = 0;
        while (scan < static_cast<int>(chunkLen)) {
            int hit = find_bytes(chunk, chunkLen, kLnkSignature, sizeof(kLnkSignature), static_cast<size_t>(scan));
            if (hit < 0 || hit >= footerOffset - chunkStart) {
                break;
            }
            linkOffsets.push_back(hit);
            scan = hit + 1;
        }

        for (size_t i = 0; i < linkOffsets.size(); ++i) {
            int start = linkOffsets[i];
            int end = i + 1 < linkOffsets.size() ? linkOffsets[i + 1] : footerOffset - chunkStart;
            if (i + 1 < linkOffsets.size() && end - 16 > start) {
                const uint8_t* candidate = chunk + end - 16;
                if (memcmp(candidate, kLnkSignature + 4, 16) == 0) {
                    end -= 16;
                }
            }
            if (end <= start) {
                continue;
            }
            std::vector<uint8_t> linkBytes(chunk + start, chunk + end);
            Entry e;
            e.automatic = false;
            e.appId = info.appId;
            e.appName = info.appName;
            e.category = categoryLabel;
            e.categoryType = type;
            e.entryNumber = static_cast<int>(i);
            e.mruPosition = static_cast<int>(i);
            LnkInfo lnk;
            std::wstring linkError;
            if (parse_lnk(linkBytes, 1252, lnk, linkError)) {
                fill_from_lnk(e, lnk);
            } else {
                e.notes = L"link could not be read: " + linkError;
            }
            if (e.target.empty()) {
                e.target = L"(shell item with no path)";
            }
            e.lastModified = e.targetAccessed > 0.0 ? e.targetAccessed : e.targetModified;
            if (e.lastModified <= 0.0) {
                e.lastModified = e.targetCreated;
            }
            e.sortTime = e.lastModified;
            e.entryCreated = e.targetCreated;
            build_search(e);
            out.push_back(std::move(e));
        }
        ++categoryIndex;
        chunkStart += chunkSize;
    }
    info.categories = categoryIndex;
    info.entryCount = static_cast<int>(out.size());
    if (!notes.empty()) {
        info.notes = join(notes, L"; ");
    }
    (void)categories;
}

}

bool read_file_bytes(const std::wstring& path, std::vector<uint8_t>& out, std::wstring& error) {
    out.clear();
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = L"cannot open the file (error " + format_int(GetLastError()) + L")";
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size)) {
        CloseHandle(handle);
        error = L"cannot read the file size";
        return false;
    }
    if (size.QuadPart < 0 || size.QuadPart > (512ll << 20)) {
        CloseHandle(handle);
        error = L"the file is larger than the 512 MB safety limit";
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < out.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(out.size() - offset, 8u << 20));
        DWORD read = 0;
        if (!ReadFile(handle, out.data() + offset, chunk, &read, nullptr) || read == 0) {
            break;
        }
        offset += read;
    }
    CloseHandle(handle);
    out.resize(offset);
    if (out.empty()) {
        error = L"the file is empty";
        return false;
    }
    return true;
}

ParseOutcome parse_jump_list_file(const std::wstring& path, Dataset& dataset, SourceInfo& info) {
    ParseOutcome outcome;
    std::vector<uint8_t> data;
    std::wstring error;
    if (!read_file_bytes(path, data, error)) {
        info = base_source(path, 0);
        info.notes = error;
        outcome.error = error;
        return outcome;
    }
    info = base_source(path, static_cast<uint64_t>(data.size()));
    std::vector<Entry> entries;
    bool automatic = data.size() >= 8 && memcmp(data.data(), kCfbMagic, 8) == 0;
    outcome.automatic = automatic;
    info.automatic = automatic;
    if (automatic) {
        outcome.parsed = parse_automatic(data, path, info, entries, error);
    } else {
        parse_custom(data, path, info, entries, error);
        outcome.parsed = error.empty();
    }
    if (!outcome.parsed) {
        info.notes = error;
        outcome.error = error;
        return outcome;
    }
    info.parsed = true;
    info.entryCount = static_cast<int>(entries.size());
    dataset.entries.insert(dataset.entries.end(), entries.begin(), entries.end());
    ++dataset.filesParsed;
    return outcome;
}

}
