#include "destlist.h"

#include "bytes.h"
#include "text.h"

#include <shlobj.h>

#include <algorithm>

namespace jl {
namespace {

constexpr size_t kHeaderSize = 32;
constexpr int kV1PathOffset = 112;
constexpr int kV2PathOffset = 128;

std::wstring hex_bytes(const uint8_t* data, size_t size) {
    static const wchar_t* digits = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

bool guid_from_text(const std::wstring& text, GUID& out) {
    std::wstring braces = text;
    if (braces.empty() || braces.front() != L'{') {
        braces = L"{" + braces + L"}";
    }
    return CLSIDFromString(braces.c_str(), &out) == S_OK;
}

}

bool droid_time(const std::wstring& droid, double& unix_seconds, std::wstring& mac) {
    unix_seconds = 0.0;
    mac.clear();
    std::wstring digits;
    for (wchar_t ch : droid) {
        if (iswxdigit(ch)) {
            digits.push_back(ch);
        }
    }
    if (digits.size() != 32) {
        return false;
    }
    uint8_t raw[16];
    for (size_t i = 0; i < 16; ++i) {
        wchar_t pair[3] = {digits[i * 2], digits[i * 2 + 1], 0};
        raw[i] = static_cast<uint8_t>(wcstoul(pair, nullptr, 16));
    }
    if ((raw[7] >> 4) != 1) {
        return false;
    }
    uint64_t stamp = rd_u64(raw) & 0x0FFFFFFFFFFFFFFFull;
    unix_seconds = static_cast<double>(stamp) / 10000000.0 - 12219292800.0;
    wchar_t buffer[32];
    swprintf_s(buffer, L"%02X:%02X:%02X:%02X:%02X:%02X", raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);
    mac = buffer;
    return true;
}

std::wstring resolve_known_folder(const std::wstring& guid, bool& found) {
    found = false;
    GUID id{};
    if (!guid_from_text(guid, id)) {
        return std::wstring();
    }
    PWSTR path = nullptr;
    if (SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &path) != S_OK || path == nullptr) {
        return std::wstring();
    }
    std::wstring out = path;
    CoTaskMemFree(path);
    found = !out.empty();
    return out;
}

std::wstring resolve_shell_path(const std::wstring& path, std::wstring* resolved) {
    std::wstring text = trim(path);
    if (text.empty()) {
        return text;
    }
    std::wstring normalized = text;
    bool known = false;
    if (normalized.rfind(L"knownfolder:", 0) == 0) {
        std::wstring guid = normalized.substr(12);
        std::wstring folder = resolve_known_folder(guid, known);
        if (known) {
            return folder;
        }
        return text;
    }
    if (normalized.rfind(L"::", 0) != 0) {
        if (resolved != nullptr) {
            *resolved = text;
        }
        return text;
    }
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= normalized.size()) {
        size_t cut = normalized.find(L'\\', start);
        if (cut == std::wstring::npos) {
            parts.push_back(normalized.substr(start));
            break;
        }
        parts.push_back(normalized.substr(start, cut - start));
        start = cut + 1;
    }
    std::wstring out;
    for (const std::wstring& part : parts) {
        std::wstring segment = part;
        if (segment.rfind(L"::", 0) == 0) {
            segment = segment.substr(2);
        }
        if (segment.empty()) {
            continue;
        }
        std::wstring chosen = segment;
        size_t open = segment.find_first_of(L"{(");
        if (open != std::wstring::npos) {
            wchar_t closing = segment[open] == L'{' ? L'}' : L')';
            size_t close = segment.find(closing, open + 1);
            if (close != std::wstring::npos && close > open + 1) {
                std::wstring guidText = segment.substr(open + 1, close - open - 1);
                bool folderFound = false;
                std::wstring folder = resolve_known_folder(guidText, folderFound);
                if (folderFound) {
                    chosen = folder + segment.substr(close + 1);
                }
            }
        }
        if (out.empty()) {
            out = chosen;
        } else if (!out.empty() && (out.back() == L'\\' || chosen.empty())) {
            out += chosen;
        } else {
            out += L"\\" + chosen;
        }
    }
    if (resolved != nullptr) {
        *resolved = out;
    }
    return text;
}

bool parse_destlist(const std::vector<uint8_t>& data, DestList& out, std::wstring& error) {
    out = DestList();
    if (data.size() < kHeaderSize) {
        error = L"the DestList stream is smaller than its 32 byte header";
        return false;
    }
    const uint8_t* base = data.data();
    out.header.version = rd_u32(base + 0);
    out.header.entryCount = rd_u32(base + 4);
    out.header.pinnedCount = rd_u32(base + 8);
    out.header.unknown0 = rd_u32(base + 12);
    out.header.unknown1 = rd_u32(base + 16);
    out.header.unknown2 = rd_u32(base + 20);
    out.header.lastIssuedEntryId = rd_u32(base + 24);
    out.header.unknown3 = rd_u32(base + 28);

    const int pathOffset = out.header.version == 1 ? kV1PathOffset : kV2PathOffset;
    size_t index = kHeaderSize;
    int mru = 0;
    size_t guard = 0;

    while (index < data.size() && guard < 200000) {
        if (out.header.version == 1 && out.entries.size() >= out.header.entryCount) {
            break;
        }
        size_t remaining = data.size() - index;
        if (remaining < static_cast<size_t>(pathOffset) + 2) {
            out.warnings.push_back(L"trailing bytes at the end of the DestList are too short for an entry");
            break;
        }
        int pathChars = rd_i16(base + index + pathOffset);
        if (pathChars < 0) {
            out.warnings.push_back(L"a DestList entry declares a negative path length, stopping");
            break;
        }
        size_t entrySize = static_cast<size_t>(pathOffset) + 2 + static_cast<size_t>(pathChars) * 2;
        if (entrySize > remaining) {
            wchar_t buffer[160];
            swprintf_s(buffer, L"a DestList entry declares a %d character path but only %zu bytes remain, stopping",
                       pathChars, remaining);
            out.warnings.push_back(buffer);
            break;
        }
        uint32_t spsSize = 0;
        size_t stride = entrySize;
        if (out.header.version != 1) {
            if (remaining < entrySize + 4) {
                out.warnings.push_back(L"a DestList entry is missing its property store size, stopping");
                break;
            }
            spsSize = rd_u32(base + index + entrySize);
            if (spsSize > remaining - entrySize - 4) {
                out.warnings.push_back(L"a DestList entry declares more property store bytes than remain, stopping");
                break;
            }
            stride = entrySize + 4 + spsSize;
        }

        const uint8_t* block = base + index;
        DestListEntry entry;
        entry.mruPosition = mru;
        entry.checksum = static_cast<int64_t>(rd_u64(block));
        entry.volumeDroid = guid_text(block + 8);
        entry.fileDroid = guid_text(block + 24);
        entry.volumeBirthDroid = guid_text(block + 40);
        entry.fileBirthDroid = guid_text(block + 56);
        if (block[73] == 0) {
            entry.hostname = from_utf16(block + 72, 16);
        } else {
            entry.hostname = from_ansi(block + 72, 16, 20127);
        }
        entry.entryNumber = rd_i32(block + 88);
        entry.unknown0 = rd_i32(block + 92);
        entry.accessCount = rd_f32(block + 96);
        entry.lastModified = filetime_to_unix(rd_u64(block + 100));
        entry.pinStatus = rd_i32(block + 108);
        if (out.header.version > 1) {
            entry.interactionCount = rd_i32(block + 116);
            entry.unknown3 = rd_i32(block + 120);
            entry.unknown4 = rd_i32(block + 124);
        }
        entry.propertyStoreSize = static_cast<int>(spsSize);
        if (spsSize > 0) {
            entry.propertyStoreHex = hex_bytes(block + entrySize + 4, std::min<size_t>(spsSize, 256));
            if (spsSize > 256) {
                entry.propertyStoreHex += L"...";
            }
        }
        entry.path = from_utf16(block + pathOffset + 2, static_cast<size_t>(pathChars) * 2);
        entry.pathResolved = resolve_shell_path(entry.path, nullptr);
        droid_time(entry.fileDroid, entry.droidTime, entry.macAddress);

        out.entries.push_back(std::move(entry));
        index += stride;
        ++mru;
        ++guard;
    }

    if (out.header.version >= 2 && out.header.entryCount != out.entries.size()) {
        wchar_t buffer[128];
        swprintf_s(buffer, L"the DestList header counts %u entries but %zu were read", out.header.entryCount,
                   out.entries.size());
        out.warnings.push_back(buffer);
    }
    return true;
}

}
