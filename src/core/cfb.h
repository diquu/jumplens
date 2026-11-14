#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jl {

struct CfbDirEntry {
    std::wstring name;
    uint8_t kind = 0;
    uint32_t left = 0xFFFFFFFFu;
    uint32_t right = 0xFFFFFFFFu;
    uint32_t child = 0xFFFFFFFFu;
    uint32_t start = 0;
    uint64_t size = 0;
};

class CompoundFile {
public:
    bool open(std::vector<uint8_t> data, std::wstring& error);

    const std::vector<CfbDirEntry>& entries() const { return entries_; }
    const CfbDirEntry* find(const std::wstring& name) const;
    bool read(const CfbDirEntry& entry, std::vector<uint8_t>& out) const;

    const std::vector<std::wstring>& warnings() const { return warnings_; }

private:
    const uint8_t* sector(uint32_t id) const;
    void collect(uint32_t start, const std::vector<uint32_t>& table, std::vector<uint32_t>& chain) const;
    bool read_fat_chain(uint32_t start, std::vector<uint8_t>& out, uint64_t limit) const;

    std::vector<uint8_t> data_;
    uint32_t sectorSize_ = 512;
    uint32_t miniSectorSize_ = 64;
    uint32_t miniCutoff_ = 4096;
    uint16_t majorVersion_ = 3;
    std::vector<uint32_t> fat_;
    std::vector<uint32_t> miniFat_;
    std::vector<uint8_t> miniStream_;
    uint32_t rootStart_ = 0xFFFFFFFFu;
    uint64_t rootSize_ = 0;
    std::vector<CfbDirEntry> entries_;
    std::vector<std::wstring> warnings_;
};

}
