#include "scan.h"

#include "discover.h"
#include "export.h"
#include "jumplist.h"
#include "text.h"

#include <algorithm>
#include <unordered_set>

namespace jl {

void run_scan(Dataset& dataset, ScanState& state, bool everywhere) {
    dataset = Dataset();
    state.phase = 0;
    state.stop = false;
    state.finished = false;
    state.directories = 0;
    state.files = 0;
    state.found = 0;
    state.parsed = 0;
    state.entryCount = 0;
    state.budgetReached = false;
    state.skipped = 0;
    state.set_current(L"Looking for jump list files");

    double start = now_seconds();

    DiscoverRequest request;
    request.everywhere = everywhere;
    request.stop = &state.stop;
    request.progress = [&state](const std::wstring& folder, const DiscoverStats& stats) {
        state.directories = stats.directories;
        state.files = stats.files;
        state.found = stats.found;
        state.set_current(folder);
    };
    DiscoverStats stats;
    std::vector<std::wstring> files = discover_jump_lists(request, stats);
    state.directories = stats.directories;
    state.files = stats.files;
    state.found = stats.found;
    state.skipped = stats.skipped;
    state.budgetReached = stats.budgetReached;
    dataset.scanSeconds = now_seconds() - start;
    dataset.filesScanned = stats.files;

    if (state.stop.load()) {
        state.finished = true;
        return;
    }

    state.phase = 1;
    double parseStart = now_seconds();
    for (size_t i = 0; i < files.size(); ++i) {
        if (state.stop.load()) {
            break;
        }
        SourceInfo info;
        size_t before = dataset.entries.size();
        ParseOutcome outcome = parse_jump_list_file(files[i], dataset, info);
        for (size_t index = before; index < dataset.entries.size(); ++index) {
            dataset.entries[index].sourceIndex = static_cast<int>(dataset.sources.size());
        }
        if (!outcome.parsed && !outcome.error.empty()) {
            dataset.warnings.push_back(file_name_of(files[i]) + L": " + outcome.error);
        }
        dataset.sources.push_back(std::move(info));
        state.parsed = static_cast<int>(i + 1);
        state.entryCount = static_cast<int>(dataset.entries.size());
        state.set_current(files[i]);
    }
    state.phase = 2;
    dataset.seconds = now_seconds() - start;
    double parseSeconds = now_seconds() - parseStart;
    (void)parseSeconds;

    std::sort(dataset.entries.begin(), dataset.entries.end(), [](const Entry& a, const Entry& b) {
        if (a.automatic != b.automatic) {
            return a.automatic;
        }
        if (a.appName != b.appName) {
            return lower(a.appName) < lower(b.appName);
        }
        if (a.pinned != b.pinned) {
            return a.pinned;
        }
        return a.lastModified > b.lastModified;
    });
    for (size_t i = 0; i < dataset.entries.size(); ++i) {
        dataset.entries[i].id = static_cast<int>(i) + 1;
    }
    finalize_dataset(dataset);
    state.finished = true;
}

void finalize_dataset(Dataset& dataset) {
    std::unordered_set<std::wstring> hosts;
    int pinned = 0;
    double first = 0.0;
    double last = 0.0;
    for (const Entry& entry : dataset.entries) {
        if (!entry.hostname.empty()) {
            hosts.insert(lower(entry.hostname));
        }
        if (entry.pinned) {
            ++pinned;
        }
        double time = entry.lastModified > 0.0 ? entry.lastModified : entry.sortTime;
        if (time > 0.0) {
            if (first == 0.0 || time < first) {
                first = time;
            }
            if (last == 0.0 || time > last) {
                last = time;
            }
        }
    }
    dataset.hosts = static_cast<int>(hosts.size());
    dataset.pinned = pinned;
    dataset.rangeFrom = first > 0.0 ? format_time(first, false) : L"--";
    dataset.rangeTo = last > 0.0 ? format_time(last, false) : L"--";
}

}
