#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

#include "core/discover.h"
#include "core/export.h"
#include "core/scan.h"
#include "core/text.h"
#include "ui/ui.h"

#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

void open_console() {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    bool redirected = output != nullptr && output != INVALID_HANDLE_VALUE &&
                      GetFileType(output) != FILE_TYPE_UNKNOWN;
    if (redirected) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) != 0 || AllocConsole() != 0) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
        freopen_s(&stream, "CONIN$", "r", stdin);
    }
}

void print_usage() {
    wprintf(L"jumplens\n");
    wprintf(L"  jumplens                       open the window and scan every volume\n");
    wprintf(L"  jumplens --standard            open the window and scan the usual Recent folders\n");
    wprintf(L"  jumplens --csv [file]          scan and write a CSV, no window\n");
    wprintf(L"  jumplens --paths               list the jump list files that would be parsed\n");
}

int run_cli(const std::vector<std::wstring>& args) {
    using namespace jl;
    bool everywhere = true;
    bool wantPaths = false;
    bool wantCsv = false;
    std::wstring csvPath;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& arg = args[i];
        if (arg == L"--standard") {
            everywhere = false;
        } else if (arg == L"--everywhere") {
            everywhere = true;
        } else if (arg == L"--paths") {
            wantPaths = true;
        } else if (arg == L"--csv") {
            wantCsv = true;
            if (i + 1 < args.size() && args[i + 1].rfind(L"--", 0) != 0) {
                csvPath = args[i + 1];
                ++i;
            }
        } else if (arg == L"--help" || arg == L"-h") {
            print_usage();
            return 0;
        }
    }

    if (!wantPaths && !wantCsv) {
        print_usage();
        return 2;
    }

    if (wantPaths) {
        DiscoverRequest request;
        request.everywhere = everywhere;
        DiscoverStats stats;
        std::vector<std::wstring> files = discover_jump_lists(request, stats);
        for (const std::wstring& file : files) {
            wprintf(L"%s\n", file.c_str());
        }
        wprintf(L"\n%zu jump list files in %d directories (%d files visited)\n", files.size(), stats.directories,
                stats.files);
        if (stats.budgetReached) {
            wprintf(L"the walk hit its entry budget before finishing\n");
        }
        if (!wantCsv) {
            return 0;
        }
    }

    ScanState state;
    Dataset dataset;
    run_scan(dataset, state, everywhere);
    if (csvPath.empty()) {
        wchar_t buffer[MAX_PATH];
        DWORD length = GetCurrentDirectoryW(MAX_PATH, buffer);
        std::wstring folder = length > 0 ? std::wstring(buffer) + L"\\" : std::wstring();
        csvPath = folder + default_export_name();
    }
    std::wstring error;
    if (!write_csv(csvPath, dataset, error)) {
        wprintf(L"export failed: %s\n", error.c_str());
        return 1;
    }
    wprintf(L"%zu entries from %zu files written to %s\n", dataset.entries.size(), dataset.sources.size(),
            csvPath.c_str());
    if (!dataset.warnings.empty()) {
        for (const std::wstring& warning : dataset.warnings) {
            wprintf(L"warning: %s\n", warning.c_str());
        }
    }
    return 0;
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    LocalFree(argv);

    for (const std::wstring& arg : args) {
        if (arg == L"--csv" || arg == L"--paths" || arg == L"--help" || arg == L"-h") {
            open_console();
            return run_cli(args);
        }
    }
    bool everywhere = true;
    for (const std::wstring& arg : args) {
        if (arg == L"--standard") {
            everywhere = false;
        }
    }
    return jl::ui::run(instance, showCommand, everywhere);
}
