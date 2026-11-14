#include "../src/core/appids.h"
#include "../src/core/cfb.h"
#include "../src/core/discover.h"
#include "../src/core/jumplist.h"
#include "../src/core/lnk.h"
#include "../src/core/scan.h"
#include "../src/core/text.h"

#include <cstdio>
#include <string>
#include <vector>

static int dump_directory(const std::wstring& path) {
    using namespace jl;
    std::vector<uint8_t> data;
    std::wstring error;
    if (!read_file_bytes(path, data, error)) {
        wprintf(L"read failed: %s\n", error.c_str());
        return 1;
    }
    CompoundFile cfb;
    if (!cfb.open(data, error)) {
        wprintf(L"cfb failed: %s\n", error.c_str());
        return 1;
    }
    for (const CfbDirEntry& entry : cfb.entries()) {
        wprintf(L"kind=%u size=%llu start=%u name=[%s]\n", entry.kind,
                static_cast<unsigned long long>(entry.size), entry.start, entry.name.c_str());
        if (entry.kind == 2) {
            std::vector<uint8_t> payload;
            cfb.read(entry, payload);
            wprintf(L"     read=%zu first4=%02X %02X %02X %02X\n", payload.size(),
                    payload.size() > 3 ? payload[0] : 0, payload.size() > 3 ? payload[1] : 0,
                    payload.size() > 3 ? payload[2] : 0, payload.size() > 3 ? payload[3] : 0);
        }
    }
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    using namespace jl;
    if (argc < 2) {
        wprintf(L"usage: parse_test <file|folder> [--everywhere] | --dir <file> | --lnk <file>\n");
        return 2;
    }
    if (wcscmp(argv[1], L"--dir") == 0 && argc > 2) {
        return dump_directory(argv[2]);
    }
    if (wcscmp(argv[1], L"--lnk") == 0 && argc > 2) {
        std::vector<uint8_t> data;
        std::wstring error;
        if (!read_file_bytes(argv[2], data, error)) {
            wprintf(L"read failed: %s\n", error.c_str());
            return 1;
        }
        LnkInfo info;
        if (!parse_lnk(data, 1252, info, error)) {
            wprintf(L"lnk failed: %s\n", error.c_str());
            return 1;
        }
        wprintf(L"size=%zu flags=%08X\n", data.size(), info.flags);
        wprintf(L"target=[%s]\n", info.targetPath.c_str());
        wprintf(L"idlist=[%s]\n", info.idListPath.c_str());
        wprintf(L"name=[%s] len=%zu\n", info.name.c_str(), info.name.size());
        wprintf(L"workdir=[%s] len=%zu\n", info.workingDir.c_str(), info.workingDir.size());
        wprintf(L"args=[%s] len=%zu\n", info.arguments.c_str(), info.arguments.size());
        wprintf(L"icon=[%s] len=%zu\n", info.iconLocation.c_str(), info.iconLocation.size());
        wprintf(L"blocks=[%s]\n", info.extraBlocks.c_str());
        wprintf(L"machineId=[%s]\n", info.machineId.c_str());
        wprintf(L"tracker volumeId=[%s]\n", info.volumeId.c_str());
        wprintf(L"tracker fileId=[%s]\n", info.fileId.c_str());
        wprintf(L"tracker volumeBirth=[%s]\n", info.volumeBirth.c_str());
        wprintf(L"tracker fileBirth=[%s]\n", info.fileBirth.c_str());
        return 0;
    }
    std::vector<std::wstring> files;
    std::wstring target = argv[1];
    DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        files.push_back(target);
    } else {
        DiscoverRequest request;
        request.everywhere = argc > 2 && wcscmp(argv[2], L"--everywhere") == 0;
        DiscoverStats stats;
        if (request.everywhere) {
            files = discover_jump_lists(request, stats);
        } else {
            WIN32_FIND_DATAW data{};
            std::wstring pattern = target + L"\\*";
            HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
            if (handle != INVALID_HANDLE_VALUE) {
                do {
                    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                        continue;
                    }
                    if (contains_ci(data.cFileName, L".automaticdestinations-ms") ||
                        contains_ci(data.cFileName, L".customdestinations-ms")) {
                        files.push_back(target + L"\\" + data.cFileName);
                    }
                } while (FindNextFileW(handle, &data));
                FindClose(handle);
            }
        }
    }

    Dataset dataset;
    int parsed = 0;
    for (const std::wstring& file : files) {
        SourceInfo info;
        ParseOutcome outcome = parse_jump_list_file(file, dataset, info);
        if (!outcome.parsed) {
            wprintf(L"FAIL %s :: %s\n", file_name_of(file).c_str(), outcome.error.c_str());
            continue;
        }
        ++parsed;
        wprintf(L"OK   %s  app=%s  version=%d  entries=%d  pinned=%d  size=%llu  type=%s\n",
                info.name.c_str(), info.appName.c_str(), info.destListVersion, info.entryCount, info.pinnedCount,
                static_cast<unsigned long long>(info.size), info.automatic ? L"automatic" : L"custom");
        if (!info.notes.empty()) {
            wprintf(L"     notes: %s\n", info.notes.c_str());
        }
    }
    finalize_dataset(dataset);
    wprintf(L"\nfiles=%zu parsed=%d entries=%zu hosts=%d pinned=%d range=%s .. %s\n", files.size(), parsed,
            dataset.entries.size(), dataset.hosts, dataset.pinned, dataset.rangeFrom.c_str(),
            dataset.rangeTo.c_str());
    int shown = 0;
    for (const Entry& e : dataset.entries) {
        if (shown++ >= 40) {
            break;
        }
        wprintf(L"  [%s] %-38s | %s | %s | %s\n", e.pinned ? L"*" : L" ", e.appName.c_str(),
                format_time(e.lastModified, false).c_str(), format_int(e.interactions).c_str(), e.target.c_str());
        if (!e.arguments.empty()) {
            wprintf(L"        args: %s\n", e.arguments.c_str());
        }
    }
    return 0;
}
