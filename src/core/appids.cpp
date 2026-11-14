#include "appids.h"

#include "appids_data.h"
#include "text.h"

#include <unordered_map>

namespace jl {
namespace {

const std::unordered_map<std::wstring, std::wstring>& table() {
    static const std::unordered_map<std::wstring, std::wstring> map = [] {
        std::unordered_map<std::wstring, std::wstring> built;
        built.reserve(static_cast<size_t>(kAppIdCount) * 2);
        for (int i = 0; i < kAppIdCount; ++i) {
            built.emplace(lower(kAppIds[i].id), kAppIds[i].name);
        }
        return built;
    }();
    return map;
}

}

int app_id_count() {
    return kAppIdCount;
}

std::wstring app_id_name(const std::wstring& appId) {
    if (appId.empty()) {
        return std::wstring();
    }
    const auto& map = table();
    auto it = map.find(lower(appId));
    if (it == map.end()) {
        return std::wstring();
    }
    return it->second;
}

std::wstring app_id_label(const std::wstring& appId) {
    std::wstring name = app_id_name(appId);
    if (!name.empty()) {
        return name;
    }
    if (appId.empty()) {
        return L"Unknown AppId";
    }
    return L"Unknown AppId " + appId;
}

}
