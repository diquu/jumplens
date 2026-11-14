#pragma once

#include "model.h"

#include <atomic>
#include <mutex>
#include <string>

namespace jl {

struct ScanState {
    std::atomic<int> phase{0};
    std::atomic<int> directories{0};
    std::atomic<int> files{0};
    std::atomic<int> found{0};
    std::atomic<int> parsed{0};
    std::atomic<int> entryCount{0};
    std::atomic<int> skipped{0};
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> budgetReached{false};
    std::mutex textLock;
    std::wstring current;

    void set_current(const std::wstring& value) {
        std::lock_guard<std::mutex> guard(textLock);
        current = value;
    }

    std::wstring get_current() {
        std::lock_guard<std::mutex> guard(textLock);
        return current;
    }
};

void run_scan(Dataset& dataset, ScanState& state, bool everywhere);
void finalize_dataset(Dataset& dataset);

}
