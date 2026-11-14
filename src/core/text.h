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

std::wstring from_utf16(const uint8_t* data, size_t bytes);
std::wstring from_ansi(const uint8_t* data, size_t bytes, UINT codepage = 1252);
std::wstring lower(std::wstring value);
bool iequals(const std::wstring& a, const std::wstring& b);
bool contains_ci(const std::wstring& haystack, const std::wstring& needle);
bool ends_with_ci(const std::wstring& value, const std::wstring& suffix);
std::wstring trim(const std::wstring& value);
std::wstring format_int(long long value);
std::wstring format_size(uint64_t bytes);
std::wstring format_hex(uint64_t value, int digits);
std::wstring format_time(double unix_seconds, bool with_seconds = false);
std::wstring format_relative(double unix_seconds, double now);
double now_seconds();
double boot_seconds();
std::string to_utf8(const std::wstring& value);
std::wstring from_utf8(const std::string& value);
std::wstring guid_text(const uint8_t* data);
std::wstring join(const std::vector<std::wstring>& parts, const std::wstring& separator);
std::wstring file_name_of(const std::wstring& path);
std::wstring parent_of(const std::wstring& path);
std::wstring base_name_of(const std::wstring& path);

}
