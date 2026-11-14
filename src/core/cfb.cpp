#include "cfb.h"

#include "bytes.h"
#include "text.h"

#include <algorithm>

namespace jl {
namespace {

constexpr uint32_t kFree = 0xFFFFFFFFu;
constexpr uint32_t kEnd = 0xFFFFFFFEu;
constexpr uint32_t kMaxReg = 0xFFFFFFFAu;
const uint8_t kMagic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

}

const uint8_t* CompoundFile::sector(uint32_t id) const {
    size_t offset = (static_cast<size_t>(id) + 1) * sectorSize_;
    if (offset + sectorSize_ > data_.size()) {
        return nullptr;
    }
    return data_.data() + offset;
}

void CompoundFile::collect(uint32_t start, const std::vector<uint32_t>& table, std::vector<uint32_t>& chain) const {
    uint32_t id = start;
    size_t guard = 0;
    std::vector<uint32_t> seen;
    while (id <= kMaxReg && id < table.size() && guard < table.size() + 8) {
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
            break;
        }
        seen.push_back(id);
        chain.push_back(id);
        id = table[id];
        ++guard;
    }
}

bool CompoundFile::read_fat_chain(uint32_t start, std::vector<uint8_t>& out, uint64_t limit) const {
    std::vector<uint32_t> chain;
    collect(start, fat_, chain);
    for (uint32_t id : chain) {
        if (out.size() >= limit) {
            break;
        }
        const uint8_t* block = sector(id);
        if (block == nullptr) {
            break;
        }
        size_t take = static_cast<size_t>(std::min<uint64_t>(sectorSize_, limit - out.size()));
        out.insert(out.end(), block, block + take);
    }
    return true;
}

bool CompoundFile::open(std::vector<uint8_t> data, std::wstring& error) {
    data_ = std::move(data);
    if (data_.size() < 512) {
        error = L"file is smaller than a compound file header";
        return false;
    }
    if (memcmp(data_.data(), kMagic, 8) != 0) {
        error = L"not an OLE compound file";
        return false;
    }
    uint16_t sectorShift = rd_u16(data_.data() + 0x1E);
    uint16_t miniShift = rd_u16(data_.data() + 0x20);
    majorVersion_ = rd_u16(data_.data() + 0x1A);
    if (sectorShift != 9 && sectorShift != 12) {
        error = L"unexpected sector size";
        return false;
    }
    if (miniShift != 6) {
        error = L"unexpected mini sector size";
        return false;
    }
    sectorSize_ = 1u << sectorShift;
    miniSectorSize_ = 1u << miniShift;
    miniCutoff_ = rd_u32(data_.data() + 0x38);
    if (miniCutoff_ == 0) {
        miniCutoff_ = 4096;
    }
    uint32_t numFatSectors = rd_u32(data_.data() + 0x2C);
    uint32_t firstDirSector = rd_u32(data_.data() + 0x30);
    uint32_t firstMiniFat = rd_u32(data_.data() + 0x3C);
    uint32_t firstDifat = rd_u32(data_.data() + 0x44);
    uint32_t numDifatSectors = rd_u32(data_.data() + 0x48);

    std::vector<uint32_t> difat;
    for (int i = 0; i < 109; ++i) {
        uint32_t value = rd_u32(data_.data() + 0x4C + 4 * i);
        if (value <= kMaxReg) {
            difat.push_back(value);
        }
    }
    uint32_t difatId = firstDifat;
    uint32_t guard = 0;
    size_t perSector = sectorSize_ / 4 - 1;
    while (difatId <= kMaxReg && guard <= numDifatSectors + 8) {
        const uint8_t* block = sector(difatId);
        if (block == nullptr) {
            break;
        }
        for (size_t i = 0; i < perSector; ++i) {
            uint32_t value = rd_u32(block + 4 * i);
            if (value <= kMaxReg) {
                difat.push_back(value);
            }
        }
        difatId = rd_u32(block + sectorSize_ - 4);
        ++guard;
    }

    size_t fatSectorCount = numFatSectors > 0 ? numFatSectors : difat.size();
    fatSectorCount = std::min(fatSectorCount, difat.size());
    fat_.clear();
    fat_.reserve(fatSectorCount * (sectorSize_ / 4));
    for (size_t i = 0; i < fatSectorCount; ++i) {
        const uint8_t* block = sector(difat[i]);
        if (block == nullptr) {
            error = L"FAT sector is past the end of the file";
            return false;
        }
        for (size_t j = 0; j < sectorSize_ / 4; ++j) {
            fat_.push_back(rd_u32(block + 4 * j));
        }
    }
    if (fat_.empty()) {
        error = L"no FAT sectors";
        return false;
    }

    std::vector<uint8_t> directory;
    if (firstDirSector > kMaxReg) {
        error = L"no directory sectors";
        return false;
    }
    read_fat_chain(firstDirSector, directory, static_cast<uint64_t>(directory.size()) + 64ull * 1024ull * 1024ull);
    if (directory.size() < 128) {
        error = L"empty directory";
        return false;
    }
    entries_.clear();
    for (size_t offset = 0; offset + 128 <= directory.size(); offset += 128) {
        const uint8_t* block = directory.data() + offset;
        uint8_t kind = block[0x42];
        if (kind != 1 && kind != 2 && kind != 5) {
            continue;
        }
        CfbDirEntry entry;
        uint16_t nameBytes = rd_u16(block + 0x40);
        nameBytes = static_cast<uint16_t>(std::min<uint16_t>(nameBytes, 64));
        size_t chars = nameBytes >= 2 ? (nameBytes - 2) / 2 : 0;
        entry.name = from_utf16(block, chars * 2);
        entry.kind = kind;
        entry.left = rd_u32(block + 0x44);
        entry.right = rd_u32(block + 0x48);
        entry.child = rd_u32(block + 0x4C);
        entry.start = rd_u32(block + 0x74);
        entry.size = rd_u64(block + 0x78);
        if (majorVersion_ == 3) {
            entry.size &= 0xFFFFFFFFull;
        }
        entries_.push_back(std::move(entry));
    }
    for (const CfbDirEntry& entry : entries_) {
        if (entry.kind == 5) {
            rootStart_ = entry.start;
            rootSize_ = entry.size;
            break;
        }
    }
    if (rootStart_ <= kMaxReg && rootSize_ > 0) {
        read_fat_chain(rootStart_, miniStream_, rootSize_);
        miniStream_.resize(static_cast<size_t>(std::min<uint64_t>(miniStream_.size(), rootSize_)));
    }
    miniFat_.clear();
    if (firstMiniFat <= kMaxReg) {
        std::vector<uint8_t> raw;
        read_fat_chain(firstMiniFat, raw, static_cast<uint64_t>(raw.size()) + 64ull * 1024ull * 1024ull);
        for (size_t i = 0; i + 4 <= raw.size(); i += 4) {
            miniFat_.push_back(rd_u32(raw.data() + i));
        }
    }
    return true;
}

const CfbDirEntry* CompoundFile::find(const std::wstring& name) const {
    for (const CfbDirEntry& entry : entries_) {
        if (entry.kind == 2 && iequals(entry.name, name)) {
            return &entry;
        }
    }
    return nullptr;
}

bool CompoundFile::read(const CfbDirEntry& entry, std::vector<uint8_t>& out) const {
    out.clear();
    if (entry.kind != 2 || entry.size == 0) {
        return true;
    }
    if (entry.size < miniCutoff_) {
        std::vector<uint32_t> chain;
        collect(entry.start, miniFat_, chain);
        for (uint32_t id : chain) {
            if (out.size() >= entry.size) {
                break;
            }
            size_t offset = static_cast<size_t>(id) * miniSectorSize_;
            if (offset + miniSectorSize_ > miniStream_.size()) {
                break;
            }
            size_t take = static_cast<size_t>(std::min<uint64_t>(miniSectorSize_, entry.size - out.size()));
            out.insert(out.end(), miniStream_.begin() + offset, miniStream_.begin() + offset + take);
        }
        return true;
    }
    return read_fat_chain(entry.start, out, entry.size);
}

}
