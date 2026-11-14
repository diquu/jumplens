#pragma once

#include "model.h"

#include <string>

namespace jl {

bool write_csv(const std::wstring& path, const Dataset& dataset, std::wstring& error);
bool write_summary(const std::wstring& path, const Dataset& dataset, std::wstring& error);
std::wstring default_export_name();

}
