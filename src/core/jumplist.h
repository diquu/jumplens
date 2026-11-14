#pragma once

#include "model.h"

#include <string>

namespace jl {

struct ParseOutcome {
    bool parsed = false;
    bool automatic = true;
    std::wstring error;
};

bool read_file_bytes(const std::wstring& path, std::vector<uint8_t>& out, std::wstring& error);
ParseOutcome parse_jump_list_file(const std::wstring& path, Dataset& dataset, SourceInfo& info);

}
