#include "discover.h"

#include "text.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <mutex>
#include <thread>

namespace jl {
namespace {

constexpr size_t kMaxEntries = 6000000;
constexpr int kMaxDepth = 96;

bool contains_ci_nofree(const std::wstring& haystack, const wchar_t* needle) {
    size_t n = wcslen(needle);
    size_t m = haystack.size();
    if (n == 0 || n > m) {
        return false;
    }
    for (size_t i = 0; i + n <= m; ++i) {
        size_t j = 0;
        for (; j < n; ++j) {
            if (towlower(haystack[i + j]) != towlower(needle[j])) {
                break;
            }
        }
        if (j == n) {
            return true;
        }
    }
    return false;
}

bool jump_list_name(const std::wstring& name) {
    return contains_ci_nofree(name, L".automaticdestinations-ms") ||
           contains_ci_nofree(name, L".customdestinations-ms");
}

bool skip_entry(const WIN32_FIND_DATAW& data) {
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return true;
    }
    if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
        return true;
    }
    return false;
}

struct Frame {
    std::wstring path;
    int depth = 0;
};

struct Shared {
    const DiscoverRequest* request = nullptr;
    std::atomic<long long> visited{0};
    std::atomic<long long> files{0};
    std::atomic<long long> directories{0};
    std::atomic<long long> found{0};
    std::atomic<long long> skipped{0};
    std::atomic<long long> pending{0};
    std::atomic<long long> lastReportMs{0};
    std::atomic<long long> abort{0};
    std::mutex queueLock;
    std::condition_variable cv;
    std::deque<Frame> queue;
    std::mutex resultsLock;
    std::vector<std::wstring> results;
    std::mutex progressLock;
};

bool stopping(const Shared& shared) {
    if (shared.abort.load() != 0) {
        return true;
    }
    return shared.request->stop != nullptr && shared.request->stop->load();
}

void report(Shared& shared, const std::wstring& folder, bool force) {
    if (shared.request->progress == nullptr) {
        return;
    }
    long long now = GetTickCount64();
    long long last = shared.lastReportMs.load();
    if (!force && now - last < 150) {
        return;
    }
    std::lock_guard<std::mutex> guard(shared.progressLock);
    if (!force && now - shared.lastReportMs.load() < 150) {
        return;
    }
    shared.lastReportMs = now;
    DiscoverStats snapshot;
    snapshot.directories = static_cast<int>(shared.directories.load());
    snapshot.files = static_cast<int>(shared.files.load());
    snapshot.found = static_cast<int>(shared.found.load());
    snapshot.skipped = static_cast<int>(shared.skipped.load());
    snapshot.budgetReached = shared.abort.load() != 0;
    snapshot.stopped = shared.request->stop != nullptr && shared.request->stop->load();
    shared.request->progress(folder, snapshot);
}

std::wstring join_path(const std::wstring& base, const std::wstring& name) {
    std::wstring out = base;
    if (!out.empty() && out.back() != L'\\') {
        out += L'\\';
    }
    out += name;
    return out;
}

void worker(Shared& shared) {
    for (;;) {
        Frame frame;
        {
            std::unique_lock<std::mutex> lock(shared.queueLock);
            shared.cv.wait(lock, [&] { return !shared.queue.empty() || shared.pending.load() == 0; });
            if (shared.queue.empty()) {
                return;
            }
            frame = std::move(shared.queue.front());
            shared.queue.pop_front();
        }

        std::wstring pattern = frame.path;
        if (!pattern.empty() && pattern.back() != L'\\') {
            pattern += L'\\';
        }
        pattern += L'*';
        WIN32_FIND_DATAW data{};
        HANDLE handle = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                         FIND_FIRST_EX_LARGE_FETCH);
        if (handle == INVALID_HANDLE_VALUE) {
            shared.skipped++;
            report(shared, frame.path, false);
            shared.pending--;
            shared.cv.notify_all();
            continue;
        }
        do {
            if (stopping(shared)) {
                break;
            }
            if (skip_entry(data)) {
                continue;
            }
            long long seen = ++shared.visited;
            if (seen > static_cast<long long>(kMaxEntries)) {
                shared.abort = 1;
                break;
            }
            bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (directory) {
                ++shared.directories;
                if (frame.depth + 1 > kMaxDepth) {
                    shared.abort = 1;
                } else {
                    std::wstring child = join_path(frame.path, data.cFileName);
                    shared.pending++;
                    {
                        std::lock_guard<std::mutex> lock(shared.queueLock);
                        shared.queue.push_back({std::move(child), frame.depth + 1});
                    }
                    shared.cv.notify_one();
                }
                report(shared, frame.path, false);
            } else {
                ++shared.files;
                if (jump_list_name(data.cFileName)) {
                    std::wstring child = join_path(frame.path, data.cFileName);
                    {
                        std::lock_guard<std::mutex> lock(shared.resultsLock);
                        shared.results.push_back(std::move(child));
                    }
                    ++shared.found;
                    report(shared, frame.path, true);
                }
            }
        } while (FindNextFileW(handle, &data) != FALSE);
        FindClose(handle);
        shared.pending--;
        shared.cv.notify_all();
    }
}

std::vector<std::wstring> standard_roots() {
    std::vector<std::wstring> roots;
    wchar_t buffer[MAX_PATH * 4];
    DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) {
        std::wstring base = buffer;
        roots.push_back(base + L"\\Microsoft\\Windows\\Recent");
    }
    length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) {
        std::wstring base = buffer;
        roots.push_back(base + L"\\Microsoft\\Windows\\Recent");
    }
    for (const wchar_t* letter : {L"C:\\Windows\\Recent", L"C:\\ProgramData\\Microsoft\\Windows\\Recent"}) {
        roots.push_back(letter);
    }
    return roots;
}

}

std::vector<std::wstring> volumes() {
    std::vector<std::wstring> roots;
    wchar_t buffer[512];
    DWORD length = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(buffer)), buffer);
    if (length == 0) {
        return roots;
    }
    for (DWORD index = 0; index < length;) {
        std::wstring drive = buffer + index;
        index += static_cast<DWORD>(drive.size()) + 1;
        UINT type = GetDriveTypeW(drive.c_str());
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_RAMDISK) {
            roots.push_back(drive);
        }
    }
    return roots;
}

std::vector<std::wstring> discover_jump_lists(const DiscoverRequest& request, DiscoverStats& stats) {
    stats = DiscoverStats();
    Shared shared;
    shared.request = &request;

    std::vector<std::wstring> roots = request.everywhere ? volumes() : standard_roots();
    std::vector<Frame> initial;
    for (const std::wstring& root : roots) {
        DWORD attributes = GetFileAttributesW(root.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        initial.push_back({root, 0});
    }
    if (request.progress) {
        DiscoverStats snapshot;
        request.progress(initial.empty() ? std::wstring() : initial.front().path, snapshot);
    }
    {
        std::lock_guard<std::mutex> lock(shared.queueLock);
        for (Frame& frame : initial) {
            shared.queue.push_back(std::move(frame));
        }
        shared.pending = static_cast<long long>(shared.queue.size());
    }

    unsigned threadCount = std::min(6u, std::max(2u, std::thread::hardware_concurrency() / 2));
    threadCount = std::max(1u, std::min<unsigned>(threadCount, static_cast<unsigned>(shared.pending.load())));
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (unsigned i = 0; i < threadCount; ++i) {
        threads.emplace_back(worker, std::ref(shared));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    stats.directories = static_cast<int>(shared.directories.load());
    stats.files = static_cast<int>(shared.files.load());
    stats.found = static_cast<int>(shared.found.load());
    stats.skipped = static_cast<int>(shared.skipped.load());
    stats.budgetReached = shared.abort.load() != 0;
    stats.stopped = request.stop != nullptr && request.stop->load();

    std::vector<std::wstring> results = std::move(shared.results);
    std::sort(results.begin(), results.end());
    results.erase(std::unique(results.begin(), results.end()), results.end());
    return results;
}

}
