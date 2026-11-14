#include "export.h"

#include "text.h"

#include <cstdio>

namespace jl {
namespace {

std::wstring quote(const std::wstring& value) {
    if (value.find_first_of(L",\"\r\n") == std::wstring::npos) {
        return value;
    }
    std::wstring out = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') {
            out += L"\"\"";
        } else {
            out.push_back(ch);
        }
    }
    out += L"\"";
    return out;
}

std::wstring time_cell(double value, bool with_seconds = true) {
    if (!(value > 0.0)) {
        return std::wstring();
    }
    return format_time(value, with_seconds);
}

void add(std::vector<std::wstring>& row, const std::wstring& value) {
    row.push_back(value);
}

}

std::wstring default_export_name() {
    wchar_t buffer[64];
    SYSTEMTIME st{};
    GetLocalTime(&st);
    swprintf_s(buffer, L"jumplens-%04d%02d%02d-%02d%02d%02d.csv", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
               st.wSecond);
    return buffer;
}

bool write_csv(const std::wstring& path, const Dataset& dataset, std::wstring& error) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
        error = L"cannot open the export file for writing";
        return false;
    }
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    fwrite(bom, 1, 3, file);

    std::vector<std::wstring> header = {
        L"Source file",        L"Source type",     L"AppID",           L"Application",      L"Category",
        L"MRU position",       L"Entry number",    L"Pinned",          L"Interactions",     L"Entry created",
        L"Last modified",      L"Hostname",        L"MAC address",     L"Target path",      L"Arguments",
        L"Working directory",  L"Target created",  L"Target modified", L"Target accessed",  L"File size",
        L"Attributes",         L"Drive type",      L"Volume serial",   L"Volume label",     L"Network name",
        L"Machine ID",         L"Volume ID",       L"File ID",         L"Volume birth",     L"File birth",
        L"DestList path",      L"Resolved path",   L"IDList path",     L"Link flags",       L"Show command",
        L"Hotkey",             L"Icon location",   L"Extra blocks",    L"Relative path",    L"Orphan stream",
        L"Notes"};

    std::wstring line;
    for (size_t i = 0; i < header.size(); ++i) {
        if (i > 0) {
            line += L",";
        }
        line += quote(header[i]);
    }
    line += L"\r\n";
    std::string utf8 = to_utf8(line);
    fwrite(utf8.data(), 1, utf8.size(), file);

    for (const Entry& e : dataset.entries) {
        const SourceInfo* source = nullptr;
        if (e.sourceIndex >= 0 && static_cast<size_t>(e.sourceIndex) < dataset.sources.size()) {
            source = &dataset.sources[static_cast<size_t>(e.sourceIndex)];
        }
        std::vector<std::wstring> row;
        add(row, source != nullptr ? source->path : L"");
        add(row, e.automatic ? L"AutomaticDestinations" : L"CustomDestinations");
        add(row, e.appId);
        add(row, e.appName);
        add(row, e.category);
        add(row, format_int(e.mruPosition));
        add(row, format_int(e.entryNumber));
        add(row, e.pinned ? L"true" : L"false");
        add(row, format_int(e.interactions));
        add(row, time_cell(e.entryCreated));
        add(row, time_cell(e.lastModified));
        add(row, e.hostname);
        add(row, e.macAddress);
        add(row, e.target);
        add(row, e.arguments);
        add(row, e.workingDir);
        add(row, time_cell(e.targetCreated));
        add(row, time_cell(e.targetModified));
        add(row, time_cell(e.targetAccessed));
        add(row, format_int(static_cast<long long>(e.fileSize)));
        add(row, e.attributes);
        add(row, e.driveType);
        add(row, e.volumeSerial);
        add(row, e.volumeLabel);
        add(row, e.networkName);
        add(row, e.machineId);
        add(row, e.trackerVolumeId);
        add(row, e.trackerFileId);
        add(row, e.trackerVolumeBirth);
        add(row, e.trackerFileBirth);
        add(row, e.objectPath);
        add(row, e.objectPathResolved);
        add(row, e.idListPath);
        add(row, e.linkFlags);
        add(row, e.showCommand);
        add(row, e.hotkey);
        add(row, e.iconLocation);
        add(row, e.extraBlocks);
        add(row, e.relativePath);
        add(row, e.orphanStream ? L"true" : L"false");
        add(row, e.notes);

        line.clear();
        for (size_t i = 0; i < row.size(); ++i) {
            if (i > 0) {
                line += L",";
            }
            line += quote(row[i]);
        }
        line += L"\r\n";
        utf8 = to_utf8(line);
        fwrite(utf8.data(), 1, utf8.size(), file);
    }
    fclose(file);
    return true;
}

bool write_summary(const std::wstring& path, const Dataset& dataset, std::wstring& error) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
        error = L"cannot open the export file for writing";
        return false;
    }
    std::wstring text;
    text += L"jumplens summary\r\n";
    text += L"sources: " + format_int(static_cast<long long>(dataset.sources.size())) + L"\r\n";
    text += L"entries: " + format_int(static_cast<long long>(dataset.entries.size())) + L"\r\n";
    for (const SourceInfo& source : dataset.sources) {
        text += L"\r\n" + source.path + L"\r\n  type: " + (source.automatic ? L"automatic" : L"custom") +
                L"\r\n  entries: " + format_int(source.entryCount) + L"\r\n";
        if (!source.notes.empty()) {
            text += L"  notes: " + source.notes + L"\r\n";
        }
    }
    std::string utf8 = to_utf8(text);
    fwrite(utf8.data(), 1, utf8.size(), file);
    fclose(file);
    return true;
}

}
