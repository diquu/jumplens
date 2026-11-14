#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace jl {

struct DiscoverStats {
    int directories = 0;
    int files = 0;
    int found = 0;
    bool budgetReached = false;
    bool stopped = false;
};

struct DiscoverRequest {
    bool everywhere = true;
    std::atomic<bool>* stop = nullptr;
    std::function<void(const std::wstring&, const DiscoverStats&)> progress;
};

std::vector<std::wstring> volumes();
std::vector<std::wstring> discover_jump_lists(const DiscoverRequest& request, DiscoverStats& stats);

}
