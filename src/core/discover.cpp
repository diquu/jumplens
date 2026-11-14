#include "discover.h"

#include "text.h"

#include <algorithm>

namespace jl {
namespace {

constexpr size_t kMaxEntries = 6000000;
constexpr int kMaxDepth = 96;

bool jump_list_name(const std::wstring& name) {
    return contains_ci(name, L".automaticdestinations-ms") || contains_ci(name, L".customdestinations-ms");
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

struct Walker {
    DiscoverRequest request;
    DiscoverStats* stats = nullptr;
    std::vector<std::wstring> results;
    size_t visited = 0;
    int lastReport = 0;
};

bool stop_requested(Walker& walker) {
    return walker.request.stop != nullptr && walker.request.stop->load();
}

void walk(Walker& walker, const std::wstring& root) {
    struct Frame {
        std::wstring path;
        int depth = 0;
    };
    std::vector<Frame> stack;
    stack.push_back({root, 0});
    while (!stack.empty()) {
        if (stop_requested(walker)) {
            walker.stats->stopped = true;
            return;
        }
        Frame frame = stack.back();
        stack.pop_back();
        std::wstring pattern = frame.path;
        if (!pattern.empty() && pattern.back() != L'\\') {
            pattern += L'\\';
        }
        pattern += L'*';
        WIN32_FIND_DATAW data{};
        HANDLE handle = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                        FIND_FIRST_EX_LARGE_FETCH);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            if (stop_requested(walker)) {
                FindClose(handle);
                walker.stats->stopped = true;
                return;
            }
            if (skip_entry(data)) {
                continue;
            }
            ++walker.visited;
            if (walker.visited > kMaxEntries) {
                walker.stats->budgetReached = true;
                FindClose(handle);
                return;
            }
            std::wstring child = frame.path;
            if (!child.empty() && child.back() != L'\\') {
                child += L'\\';
            }
            child += data.cFileName;
            bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (directory) {
                ++walker.stats->directories;
                if (frame.depth < kMaxDepth) {
                    stack.push_back({child, frame.depth + 1});
                } else {
                    walker.stats->budgetReached = true;
                }
                if (walker.stats->directories - walker.lastReport >= 400) {
                    walker.lastReport = walker.stats->directories;
                    if (walker.request.progress) {
                        walker.request.progress(frame.path, *walker.stats);
                    }
                }
            } else {
                ++walker.stats->files;
                if (jump_list_name(data.cFileName)) {
                    walker.results.push_back(child);
                    ++walker.stats->found;
                    if (walker.request.progress) {
                        walker.request.progress(frame.path, *walker.stats);
                    }
                }
            }
        } while (FindNextFileW(handle, &data));
        FindClose(handle);
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
    Walker walker;
    walker.request = request;
    walker.stats = &stats;
    std::vector<std::wstring> roots = request.everywhere ? volumes() : standard_roots();
    for (const std::wstring& root : roots) {
        DWORD attributes = GetFileAttributesW(root.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        if (request.progress) {
            request.progress(root, stats);
        }
        walk(walker, root);
        if (stats.stopped || stats.budgetReached) {
            break;
        }
    }
    std::sort(walker.results.begin(), walker.results.end());
    walker.results.erase(std::unique(walker.results.begin(), walker.results.end()), walker.results.end());
    return walker.results;
}

}
