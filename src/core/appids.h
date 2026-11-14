#pragma once

#include <string>

namespace jl {

int app_id_count();
std::wstring app_id_name(const std::wstring& appId);
std::wstring app_id_label(const std::wstring& appId);

}
