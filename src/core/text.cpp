#include "text.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace jl {

std::wstring from_utf16(const uint8_t* data, size_t bytes) {
    std::wstring out;
    if (data == nullptr || bytes < 2) {
        return out;
    }
    size_t count = bytes / 2;
    out.resize(count);
    memcpy(out.data(), data, count * 2);
    size_t cut = out.find(L'\0');
    if (cut != std::wstring::npos) {
        out.resize(cut);
    }
    return out;
}

std::wstring from_ansi(const uint8_t* data, size_t bytes, UINT codepage) {
    std::wstring out;
    if (data == nullptr || bytes == 0) {
        return out;
    }
    size_t length = 0;
    while (length < bytes && data[length] != 0) {
        ++length;
    }
    if (length == 0) {
        return out;
    }
    int wide = MultiByteToWideChar(codepage, 0, reinterpret_cast<const char*>(data), static_cast<int>(length), nullptr, 0);
    if (wide <= 0) {
        return out;
    }
    out.resize(static_cast<size_t>(wide));
    MultiByteToWideChar(codepage, 0, reinterpret_cast<const char*>(data), static_cast<int>(length), out.data(), wide);
    return out;
}

std::wstring lower(std::wstring value) {
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

bool iequals(const std::wstring& a, const std::wstring& b) {
    return lower(a) == lower(b);
}

bool contains_ci(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    return lower(haystack).find(lower(needle)) != std::wstring::npos;
}

bool ends_with_ci(const std::wstring& value, const std::wstring& suffix) {
    if (suffix.size() > value.size()) {
        return false;
    }
    return lower(value.substr(value.size() - suffix.size())) == lower(suffix);
}

std::wstring trim(const std::wstring& value) {
    size_t first = 0;
    size_t last = value.size();
    while (first < last && std::iswspace(value[first])) {
        ++first;
    }
    while (last > first && std::iswspace(value[last - 1])) {
        --last;
    }
    return value.substr(first, last - first);
}

std::wstring format_int(long long value) {
    std::wstring digits = std::to_wstring(value < 0 ? -value : value);
    std::wstring out;
    int seen = 0;
    for (size_t i = digits.size(); i > 0; --i) {
        out.push_back(digits[i - 1]);
        if (++seen % 3 == 0 && i > 1) {
            out.push_back(L',');
        }
    }
    if (value < 0) {
        out.push_back(L'-');
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::wstring format_size(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buffer[64];
    if (unit == 0) {
        swprintf_s(buffer, L"%llu B", static_cast<unsigned long long>(bytes));
    } else {
        swprintf_s(buffer, L"%.1f %s", value, units[unit]);
    }
    return buffer;
}

std::wstring format_hex(uint64_t value, int digits) {
    wchar_t buffer[64];
    swprintf_s(buffer, L"%0*llX", digits, static_cast<unsigned long long>(value));
    return buffer;
}

static bool unix_to_local_systemtime(double unix_seconds, SYSTEMTIME& out) {
    if (!(unix_seconds > -11644473600.0) || unix_seconds > 32503680000.0) {
        return false;
    }
    double ticks = (unix_seconds + 11644473600.0) * 10000000.0;
    if (ticks < 0.0) {
        return false;
    }
    uint64_t total = static_cast<uint64_t>(ticks);
    FILETIME utc;
    utc.dwLowDateTime = static_cast<DWORD>(total & 0xFFFFFFFFu);
    utc.dwHighDateTime = static_cast<DWORD>(total >> 32);
    FILETIME local;
    if (!FileTimeToLocalFileTime(&utc, &local)) {
        return false;
    }
    return FileTimeToSystemTime(&local, &out) != 0;
}

std::wstring format_time(double unix_seconds, bool with_seconds) {
    SYSTEMTIME st{};
    if (!unix_to_local_systemtime(unix_seconds, st)) {
        return L"--";
    }
    wchar_t buffer[64];
    if (with_seconds) {
        swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                   st.wSecond);
    } else {
        swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    }
    return buffer;
}

std::wstring format_relative(double unix_seconds, double now) {
    if (!(unix_seconds > 0.0)) {
        return L"";
    }
    double delta = now - unix_seconds;
    bool future = delta < 0.0;
    double value = fabs(delta);
    const wchar_t* unit = L"s";
    double amount = value;
    if (value >= 86400.0 * 365.0) {
        amount = value / (86400.0 * 365.0);
        unit = L"y";
    } else if (value >= 86400.0 * 30.0) {
        amount = value / (86400.0 * 30.0);
        unit = L"mo";
    } else if (value >= 86400.0) {
        amount = value / 86400.0;
        unit = L"d";
    } else if (value >= 3600.0) {
        amount = value / 3600.0;
        unit = L"h";
    } else if (value >= 60.0) {
        amount = value / 60.0;
        unit = L"m";
    }
    wchar_t buffer[64];
    if (unit[0] == L's' && unit[1] == 0) {
        if (future) {
            swprintf_s(buffer, L"in %llds", static_cast<long long>(amount));
        } else {
            swprintf_s(buffer, L"%llds ago", static_cast<long long>(amount));
        }
    } else if (amount < 10.0) {
        swprintf_s(buffer, future ? L"in %.1f%s" : L"%.1f%s ago", amount, unit);
    } else {
        swprintf_s(buffer, future ? L"in %.0f%s" : L"%.0f%s ago", amount, unit);
    }
    return buffer;
}

double now_seconds() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t ticks = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<double>(ticks) / 10000000.0 - 11644473600.0;
}

double boot_seconds() {
    return now_seconds() - static_cast<double>(GetTickCount64()) / 1000.0;
}

std::string to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return std::string();
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::string();
    }
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring from_utf8(const std::string& value) {
    if (value.empty()) {
        return std::wstring();
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), size);
    return out;
}

std::wstring guid_text(const uint8_t* data) {
    if (data == nullptr) {
        return std::wstring();
    }
    uint32_t d1 = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                  (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
    uint16_t d2 = static_cast<uint16_t>(data[4] | (data[5] << 8));
    uint16_t d3 = static_cast<uint16_t>(data[6] | (data[7] << 8));
    wchar_t buffer[64];
    swprintf_s(buffer, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", d1, d2, d3, data[8], data[9], data[10],
               data[11], data[12], data[13], data[14], data[15]);
    return buffer;
}

std::wstring join(const std::vector<std::wstring>& parts, const std::wstring& separator) {
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += separator;
        }
        out += parts[i];
    }
    return out;
}

std::wstring file_name_of(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring::npos) {
        return path;
    }
    return path.substr(cut + 1);
}

std::wstring parent_of(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring::npos) {
        return std::wstring();
    }
    return path.substr(0, cut);
}

std::wstring base_name_of(const std::wstring& path) {
    std::wstring name = file_name_of(path);
    size_t cut = name.find_last_of(L'.');
    if (cut == std::wstring::npos || cut == 0) {
        return name;
    }
    return name.substr(0, cut);
}

}
