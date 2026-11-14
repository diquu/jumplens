#include "lnk.h"

#include "bytes.h"
#include "text.h"

#include <algorithm>

namespace jl {
namespace {

const uint8_t kClsid[16] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};

struct FlagName {
    uint32_t bit;
    const wchar_t* name;
};

const FlagName kFlags[] = {
    {0x00000001, L"HasLinkTargetIDList"},   {0x00000002, L"HasLinkInfo"},
    {0x00000004, L"HasName"},               {0x00000008, L"HasRelativePath"},
    {0x00000010, L"HasWorkingDir"},         {0x00000020, L"HasArguments"},
    {0x00000040, L"HasIconLocation"},       {0x00000080, L"IsUnicode"},
    {0x00000100, L"ForceNoLinkInfo"},       {0x00000200, L"HasExpString"},
    {0x00000400, L"RunInSeparateProcess"},  {0x00000800, L"Unused1"},
    {0x00001000, L"HasDarwinID"},           {0x00002000, L"RunAsUser"},
    {0x00004000, L"HasExpIcon"},            {0x00008000, L"NoPidlAlias"},
    {0x00010000, L"Unused2"},               {0x00020000, L"RunWithShimLayer"},
    {0x00040000, L"ForceNoLinkTrack"},      {0x00080000, L"EnableTargetMetadata"},
    {0x00100000, L"DisableLinkPathTracking"}, {0x00200000, L"DisableKnownFolderTracking"},
    {0x00400000, L"DisableKnownFolderAlias"}, {0x00800000, L"AllowLinkToLink"},
    {0x01000000, L"UnaliasOnSave"},         {0x02000000, L"PreferEnvironmentPath"},
    {0x04000000, L"KeepLocalIDListForUNCTarget"},
};

struct AttributeName {
    uint32_t bit;
    const wchar_t* name;
};

const AttributeName kAttributes[] = {
    {0x0001, L"ReadOnly"},   {0x0002, L"Hidden"},        {0x0004, L"System"},
    {0x0010, L"Directory"},  {0x0020, L"Archive"},       {0x0040, L"Device"},
    {0x0080, L"Normal"},     {0x0100, L"Temporary"},     {0x0200, L"SparseFile"},
    {0x0400, L"ReparsePoint"}, {0x0800, L"Compressed"},  {0x1000, L"Offline"},
    {0x2000, L"NotContentIndexed"}, {0x4000, L"Encrypted"},
};

struct ExtraName {
    uint32_t signature;
    const wchar_t* name;
};

const ExtraName kExtra[] = {
    {0xA0000001, L"EnvironmentVariable"},
    {0xA0000002, L"Console"},
    {0xA0000003, L"Tracker"},
    {0xA0000004, L"ConsoleFE"},
    {0xA0000005, L"SpecialFolder"},
    {0xA0000006, L"Darwin"},
    {0xA0000007, L"IconEnvironment"},
    {0xA0000008, L"Shim"},
    {0xA0000009, L"PropertyStore"},
    {0xA000000B, L"KnownFolder"},
    {0xA000000C, L"VistaAndAboveIDList"},
};

bool has(const uint8_t* base, size_t size, size_t offset, size_t need) {
    return offset <= size && need <= size - offset;
}

std::wstring wide_z(const uint8_t* data, size_t size, size_t offset, size_t limit) {
    if (offset >= size) {
        return std::wstring();
    }
    size_t available = std::min(limit, size - offset);
    return from_utf16(data + offset, available);
}

std::wstring ansi_z(const uint8_t* data, size_t size, size_t offset, size_t limit, UINT codepage) {
    if (offset >= size) {
        return std::wstring();
    }
    size_t available = std::min(limit, size - offset);
    return from_ansi(data + offset, available, codepage);
}

std::wstring find_wide_name(const uint8_t* data, size_t size) {
    std::wstring best;
    size_t index = 0;
    while (index + 3 < size) {
        if (data[index] >= 0x20 && data[index] <= 0x7E && data[index + 1] == 0) {
            size_t start = index;
            size_t end = start;
            while (end + 1 < size && data[end] >= 0x20 && data[end] <= 0x7E && data[end + 1] == 0) {
                end += 2;
            }
            if (end - start >= 4) {
                std::wstring candidate = from_utf16(data + start, end - start);
                if (candidate.size() > best.size()) {
                    best = candidate;
                }
            }
            index = end + 2;
            continue;
        }
        index += 1;
    }
    return best;
}

std::wstring item_name(const uint8_t* block, size_t size) {
    if (size < 3) {
        return std::wstring();
    }
    uint8_t type = block[2];
    if (type == 0x1F || type == 0x2F || type == 0x31 || type == 0x32) {
        std::wstring name;
        if (type == 0x2F) {
            name = wide_z(block, size, 14, size - 14);
        } else {
            name = ansi_z(block, size, 14, size - 14, 1252);
        }
        if (type == 0x31 || type == 0x32) {
            std::wstring wide = find_wide_name(block + std::min<size_t>(14, size), size > 14 ? size - 14 : 0);
            if (!wide.empty() && wide.size() >= name.size()) {
                name = wide;
            }
        }
        return name;
    }
    if (type == 0x20) {
        return ansi_z(block, size, 4, size - 4, 1252);
    }
    if (type == 0x23) {
        return wide_z(block, size, 4, size - 4);
    }
    if (type >= 0x41 && type <= 0x4B) {
        return wide_z(block, size, 4, size - 4);
    }
    return std::wstring();
}

std::wstring parse_idlist(const uint8_t* data, size_t size) {
    if (size < 2) {
        return std::wstring();
    }
    size_t declared = rd_u16(data);
    size_t end = declared >= 2 ? std::min(declared, size) : size;
    std::vector<std::wstring> parts;
    size_t pos = 2;
    int guard = 0;
    while (pos + 2 <= end && guard < 512) {
        size_t itemSize = rd_u16(data + pos);
        if (itemSize < 2 || pos + itemSize > end) {
            break;
        }
        std::wstring name = item_name(data + pos, itemSize);
        if (!name.empty()) {
            parts.push_back(name);
        }
        pos += itemSize;
        ++guard;
    }
    std::wstring text;
    for (const std::wstring& part : parts) {
        if (text.empty()) {
            text = part;
        } else if (!text.empty() && text.back() == L'\\') {
            text += part;
        } else {
            text += L"\\" + part;
        }
    }
    return text;
}

const wchar_t* extra_name(uint32_t signature) {
    for (const ExtraName& item : kExtra) {
        if (item.signature == signature) {
            return item.name;
        }
    }
    return nullptr;
}

void parse_link_info(const uint8_t* block, size_t size, UINT codepage, LnkInfo& out) {
    if (size < 0x1C) {
        out.warnings.push_back(L"link info block is truncated");
        return;
    }
    uint32_t headerSize = rd_u32(block + 4);
    uint32_t flags = rd_u32(block + 8);
    uint32_t volumeOffset = rd_u32(block + 0x0C);
    uint32_t baseOffset = rd_u32(block + 0x10);
    uint32_t netOffset = rd_u32(block + 0x14);
    uint32_t suffixOffset = rd_u32(block + 0x18);
    uint32_t baseOffsetWide = 0;
    uint32_t suffixOffsetWide = 0;
    if (headerSize >= 0x24 && size >= 0x24) {
        baseOffsetWide = rd_u32(block + 0x1C);
        suffixOffsetWide = rd_u32(block + 0x20);
    }
    if ((flags & 0x01) != 0 && volumeOffset > 0 && volumeOffset < size) {
        const uint8_t* volume = block + volumeOffset;
        size_t volumeSize = size - volumeOffset;
        if (volumeSize >= 0x10) {
            out.driveType = rd_u32(volume + 4);
            out.volumeSerial = rd_u32(volume + 8);
            uint32_t labelOffset = rd_u32(volume + 0x0C);
            if (labelOffset == 0x14 && volumeSize >= 0x14) {
                out.volumeLabel = wide_z(volume, volumeSize, rd_u32(volume + 0x10), volumeSize);
            } else if (labelOffset > 0 && labelOffset < volumeSize) {
                out.volumeLabel = ansi_z(volume, volumeSize, labelOffset, volumeSize - labelOffset, codepage);
            }
        }
        if (baseOffsetWide > 0 && baseOffsetWide < size) {
            out.localBasePath = wide_z(block, size, baseOffsetWide, size - baseOffsetWide);
        } else if (baseOffset > 0 && baseOffset < size) {
            out.localBasePath = ansi_z(block, size, baseOffset, size - baseOffset, codepage);
        }
    }
    if (netOffset > 0 && netOffset < size) {
        const uint8_t* net = block + netOffset;
        size_t netSize = size - netOffset;
        if (netSize >= 0x14) {
            uint32_t netNameOffset = rd_u32(net + 8);
            uint32_t deviceOffset = rd_u32(net + 12);
            if (netNameOffset > 0 && netNameOffset < netSize) {
                out.networkName = ansi_z(net, netSize, netNameOffset, netSize - netNameOffset, codepage);
            }
            if (deviceOffset > 0 && deviceOffset < netSize) {
                out.networkDevice = ansi_z(net, netSize, deviceOffset, netSize - deviceOffset, codepage);
            }
        }
    }
    if (suffixOffsetWide > 0 && suffixOffsetWide < size) {
        out.commonPathSuffix = wide_z(block, size, suffixOffsetWide, size - suffixOffsetWide);
    } else if (suffixOffset > 0 && suffixOffset < size) {
        out.commonPathSuffix = ansi_z(block, size, suffixOffset, size - suffixOffset, codepage);
    }
}

void parse_extra(const uint8_t* block, size_t size, uint32_t signature, LnkInfo& out) {
    switch (signature) {
        case 0xA0000001: {
            std::wstring wide = size > 268 ? wide_z(block, size, 268, 520) : std::wstring();
            out.environmentTarget = wide.empty() ? ansi_z(block, size, 8, 260, 1252) : wide;
            break;
        }
        case 0xA0000003: {
            if (size >= 32) {
                out.machineId = from_ansi(block + 16, 16, 20127);
                if (size >= 48) {
                    out.volumeId = guid_text(block + 32);
                }
                if (size >= 64) {
                    out.fileId = guid_text(block + 48);
                }
                if (size >= 80) {
                    out.volumeBirth = guid_text(block + 64);
                }
                if (size >= 96) {
                    out.fileBirth = guid_text(block + 80);
                }
            }
            break;
        }
        case 0xA0000005: {
            break;
        }
        case 0xA0000006: {
            std::wstring wide = size > 268 ? wide_z(block, size, 268, 520) : std::wstring();
            out.darwinTarget = wide.empty() ? ansi_z(block, size, 8, 260, 1252) : wide;
            break;
        }
        case 0xA0000007: {
            break;
        }
        case 0xA0000008: {
            out.shimLayer = wide_z(block, size, 8, size > 8 ? size - 8 : 0);
            break;
        }
        case 0xA0000009: {
            out.hasPropertyStore = true;
            out.propertyStoreSize = static_cast<uint32_t>(size > 8 ? size - 8 : 0);
            break;
        }
        case 0xA000000B: {
            if (size >= 24) {
                out.knownFolder = guid_text(block + 8);
            }
            break;
        }
        case 0xA000000C: {
            if (size > 8) {
                std::wstring path = parse_idlist(block + 8, size - 8);
                if (!path.empty()) {
                    out.idListPath = path;
                }
            }
            break;
        }
        default:
            break;
    }
}

constexpr uint32_t kBlockNames[] = {0xA0000001, 0xA0000002, 0xA0000003, 0xA0000004, 0xA0000005,
                                    0xA0000006, 0xA0000007, 0xA0000008, 0xA0000009, 0xA000000B,
                                    0xA000000C};

}

const wchar_t* drive_type_name(uint32_t value) {
    switch (value) {
        case 0: return L"Unknown";
        case 1: return L"No root directory";
        case 2: return L"Removable";
        case 3: return L"Fixed";
        case 4: return L"Network";
        case 5: return L"Optical";
        case 6: return L"RAM disk";
        default: return L"Other";
    }
}

std::wstring attribute_names(uint32_t attributes) {
    std::vector<std::wstring> names;
    for (const AttributeName& entry : kAttributes) {
        if ((attributes & entry.bit) != 0) {
            names.push_back(entry.name);
        }
    }
    return join(names, L", ");
}

std::wstring hotkey_text(uint16_t value) {
    if (value == 0) {
        return std::wstring();
    }
    std::vector<std::wstring> parts;
    if (value & 0x01) parts.push_back(L"Shift");
    if (value & 0x02) parts.push_back(L"Ctrl");
    if (value & 0x04) parts.push_back(L"Alt");
    uint8_t key = static_cast<uint8_t>(value >> 8);
    wchar_t buffer[16];
    switch (key) {
        case 0x08: parts.push_back(L"Backspace"); break;
        case 0x09: parts.push_back(L"Tab"); break;
        case 0x0D: parts.push_back(L"Enter"); break;
        case 0x1B: parts.push_back(L"Esc"); break;
        case 0x20: parts.push_back(L"Space"); break;
        default:
            if (key >= 0x30 && key <= 0x5A) {
                buffer[0] = static_cast<wchar_t>(key);
                buffer[1] = 0;
                parts.push_back(buffer);
            } else {
                swprintf_s(buffer, L"0x%02X", key);
                parts.push_back(buffer);
            }
            break;
    }
    return join(parts, L"+");
}

std::wstring show_command_name(uint32_t value) {
    switch (value) {
        case 1: return L"Normal";
        case 2: return L"Maximized";
        case 3: return L"Minimized";
        case 7: return L"Minimized, no activate";
        default: return format_int(value);
    }
}

bool parse_lnk(const std::vector<uint8_t>& data, UINT codepage, LnkInfo& out, std::wstring& error) {
    if (data.size() < 0x4C) {
        error = L"link header is truncated";
        return false;
    }
    const uint8_t* base = data.data();
    size_t size = data.size();
    if (rd_u32(base) != 0x4C) {
        error = L"unexpected link header size";
        return false;
    }
    if (memcmp(base + 4, kClsid, 16) != 0) {
        error = L"link class identifier mismatch";
        return false;
    }
    out.flags = rd_u32(base + 0x14);
    out.fileAttributes = rd_u32(base + 0x18);
    out.creationTime = filetime_to_unix(rd_u64(base + 0x1C));
    out.accessTime = filetime_to_unix(rd_u64(base + 0x24));
    out.writeTime = filetime_to_unix(rd_u64(base + 0x2C));
    out.fileSize = rd_u32(base + 0x34);
    out.showCommand = rd_u32(base + 0x3C);
    out.hotkey = rd_u16(base + 0x40);
    for (const FlagName& entry : kFlags) {
        if ((out.flags & entry.bit) != 0) {
            out.flagNames.push_back(entry.name);
        }
    }
    size_t pos = 0x4C;
    if ((out.flags & 0x01) != 0) {
        if (!has(base, size, pos, 2)) {
            error = L"link target identifier list is truncated";
            return false;
        }
        size_t total = rd_u16(base + pos);
        pos += 2;
        if (total > size - pos) {
            total = size - pos;
        }
        out.idListPath = parse_idlist(base + pos, total);
        pos += total;
    }
    if ((out.flags & 0x02) != 0 && has(base, size, pos, 4)) {
        size_t blockSize = rd_u32(base + pos);
        if (blockSize < 0x1C || pos + blockSize > size) {
            out.warnings.push_back(L"link info block has an invalid size");
            blockSize = std::min(blockSize, size - pos);
        }
        parse_link_info(base + pos, blockSize, codepage, out);
        pos += blockSize;
    }
    if ((out.flags & 0x7C) != 0) {
        struct Slot {
            uint32_t bit;
            std::wstring* target;
        };
        Slot slots[] = {{0x04, &out.name},
                        {0x08, &out.relativePath},
                        {0x10, &out.workingDir},
                        {0x20, &out.arguments},
                        {0x40, &out.iconLocation}};
        std::vector<Slot> active;
        for (const Slot& slot : slots) {
            if ((out.flags & slot.bit) != 0) {
                active.push_back(slot);
            }
        }
        for (size_t i = 0; i < active.size(); ++i) {
            if (!has(base, size, pos, 2)) {
                break;
            }
            size_t length = rd_u16(base + pos);
            pos += 2;
            if ((out.flags & 0x80) != 0) {
                size_t bytes = length * 2;
                if (!has(base, size, pos, bytes)) {
                    break;
                }
                *active[i].target = from_utf16(base + pos, bytes);
                pos += bytes;
            } else {
                if (!has(base, size, pos, length)) {
                    break;
                }
                *active[i].target = from_ansi(base + pos, length, codepage);
                pos += length;
            }
        }
    }
    std::vector<std::wstring> blocks;
    while (has(base, size, pos, 8)) {
        size_t blockSize = rd_u32(base + pos);
        uint32_t signature = rd_u32(base + pos + 4);
        if (blockSize < 8 || pos + blockSize > size) {
            break;
        }
        const wchar_t* label = extra_name(signature);
        if (label != nullptr && std::find(std::begin(kBlockNames), std::end(kBlockNames), signature) !=
                                   std::end(kBlockNames)) {
            blocks.push_back(label);
        } else {
            blocks.push_back(format_hex(signature, 8));
        }
        parse_extra(base + pos, blockSize, signature, out);
        pos += blockSize;
    }
    out.extraBlocks = join(blocks, L", ");
    if (!out.localBasePath.empty() && !out.commonPathSuffix.empty()) {
        if (out.localBasePath.back() == L'\\') {
            out.targetPath = out.localBasePath + out.commonPathSuffix;
        } else {
            out.targetPath = out.localBasePath + L"\\" + out.commonPathSuffix;
        }
    } else if (!out.localBasePath.empty()) {
        out.targetPath = out.localBasePath;
    } else if (!out.commonPathSuffix.empty()) {
        out.targetPath = out.commonPathSuffix;
    } else if (!out.networkName.empty()) {
        out.targetPath = out.networkName;
    } else {
        out.targetPath = out.idListPath;
    }
    return true;
}

}
