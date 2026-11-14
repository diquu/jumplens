#include "ui.h"

#include "logo_data.h"
#include "glyph_data.h"

#include "../core/export.h"
#include "../core/scan.h"
#include "../core/signature.h"
#include "../core/text.h"

#include <d2d1.h>
#include <dwrite.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace jl {
namespace ui {
namespace {

constexpr D2D1_COLOR_F rgb(unsigned int hex, float alpha = 1.0f) {
    D2D1_COLOR_F c = {};
    c.r = ((hex >> 16) & 0xFF) / 255.0f;
    c.g = ((hex >> 8) & 0xFF) / 255.0f;
    c.b = (hex & 0xFF) / 255.0f;
    c.a = alpha;
    return c;
}

const D2D1_COLOR_F kBg = rgb(0x0B0D0E);
const D2D1_COLOR_F kPanel = rgb(0x101314);
const D2D1_COLOR_F kRaised = rgb(0x161A1B);
const D2D1_COLOR_F kHair = rgb(0x22282A);
const D2D1_COLOR_F kText = rgb(0xE9E7E2);
const D2D1_COLOR_F kDim = rgb(0x9AA3A5);
const D2D1_COLOR_F kFaint = rgb(0x6C7679);
const D2D1_COLOR_F kAmber = rgb(0xE8A33C);
const D2D1_COLOR_F kAmberSoft = rgb(0xE8A33C, 0.16f);
const D2D1_COLOR_F kSelect = rgb(0x191F20);

const wchar_t* kAppName = L"jumplens";

struct Layout {
    D2D1_RECT_F top = {};
    D2D1_RECT_F search = {};
    D2D1_RECT_F rescan = {};
    D2D1_RECT_F exportButton = {};
    D2D1_RECT_F summary = {};
    D2D1_RECT_F rail = {};
    D2D1_RECT_F modeEverywhere = {};
    D2D1_RECT_F modeStandard = {};
    D2D1_RECT_F filterBoot = {};
    D2D1_RECT_F filterNoise = {};
    D2D1_RECT_F railList = {};
    D2D1_RECT_F table = {};
    D2D1_RECT_F header = {};
    D2D1_RECT_F drawer = {};
    D2D1_RECT_F status = {};
    D2D1_RECT_F histogram = {};
    float colPin = 0.0f;
    float colNum = 0.0f;
    float colApp = 0.0f;
    float colTarget = 0.0f;
    float colCount = 0.0f;
    float colModified = 0.0f;
    float rowHeight = 56.0f;
};

struct Bucket {
    std::wstring day;
    std::wstring label;
    int count = 0;
};

enum class SortColumn {
    Modified,
    App,
    Target,
    Count,
};

struct App {
    HWND window = nullptr;
    HINSTANCE instance = nullptr;
    UINT dpi = 96;
    float scale = 1.0f;

    ID2D1Factory* d2d = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    ID2D1Bitmap* logo = nullptr;
    ID2D1Bitmap* glyphSearch = nullptr;
    ID2D1Bitmap* glyphSearchAmber = nullptr;
    ID2D1Bitmap* glyphRefresh = nullptr;
    ID2D1Bitmap* glyphDownload = nullptr;
    ID2D1Bitmap* glyphEmpty = nullptr;
    ID2D1LinearGradientBrush* brushHistogram = nullptr;
    IDWriteInlineObject* ellipsis = nullptr;

    IDWriteTextFormat* fWord = nullptr;
    IDWriteTextFormat* fHead = nullptr;
    IDWriteTextFormat* fBody = nullptr;
    IDWriteTextFormat* fBold = nullptr;
    IDWriteTextFormat* fSmall = nullptr;
    IDWriteTextFormat* fMono = nullptr;
    IDWriteTextFormat* fMonoSmall = nullptr;
    IDWriteTextFormat* fStat = nullptr;
    IDWriteTextFormat* fWrap = nullptr;
    IDWriteTextFormat* fWrapBold = nullptr;

    Dataset dataset;
    ScanState scan;
    std::thread worker;
    std::mutex doneMutex;
    std::unique_ptr<Dataset> finished;
    std::atomic<bool> done{false};
    bool scanning = false;
    bool closing = false;
    bool everywhere = true;

    std::vector<int> visible;
    int sourceFilter = -1;
    std::wstring dayFilter;
    std::wstring query;
    SortColumn sortColumn = SortColumn::Modified;
    bool sortAscending = false;

    bool reduceNoise = true;
    bool bootOnly = false;
    double bootTime = boot_seconds();
    int hiddenFilters = 0;
    int noiseHidden = 0;
    std::vector<int> sourceCounts;
    std::vector<uint8_t> noise;
    std::unique_ptr<std::vector<uint8_t>> finishedNoise;

    std::vector<Bucket> buckets;

    float scroll = 0.0f;
    int selected = -1;
    int hoverRow = -1;
    int hoverSource = -1;
    int hoverBucket = -1;
    int hoverMode = -1;
    int hoverFilter = -1;
    bool searchFocused = false;
    int caret = 0;
    float mouseX = -1.0f;
    float mouseY = -1.0f;
    float colAppW = -1.0f;
    float colTargetW = -1.0f;
    float colCountW = -1.0f;
    int dragSep = -1;
    int hoverSep = -1;
    float dragStartX = 0.0f;
    float dragBaseApp = 0.0f;
    float dragBaseTarget = 0.0f;
    float dragBaseCount = 0.0f;

    Layout layout;
    std::vector<int> railOrder;

    ~App() {
        release();
    }

    void release() {
        if (worker.joinable()) {
            scan.stop = true;
            worker.join();
        }
        drop_target();
        if (ellipsis != nullptr) {
            ellipsis->Release();
            ellipsis = nullptr;
        }
        for (IDWriteTextFormat** format : {&fWord, &fHead, &fBody, &fBold, &fSmall, &fMono, &fMonoSmall, &fStat,
                                           &fWrap, &fWrapBold}) {
            if (*format != nullptr) {
                (*format)->Release();
                *format = nullptr;
            }
        }
        if (dwrite != nullptr) dwrite->Release();
        if (d2d != nullptr) d2d->Release();
        dwrite = nullptr;
        d2d = nullptr;
    }

    IDWriteTextFormat* make_format(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight,
                                   DWRITE_WORD_WRAPPING wrap = DWRITE_WORD_WRAPPING_NO_WRAP) {
        IDWriteTextFormat* format = nullptr;
        if (dwrite == nullptr) {
            return nullptr;
        }
        if (FAILED(dwrite->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                            DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &format))) {
            return nullptr;
        }
        format->SetWordWrapping(wrap);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (wrap == DWRITE_WORD_WRAPPING_NO_WRAP && ellipsis != nullptr) {
            DWRITE_TRIMMING trimming = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            format->SetTrimming(&trimming, ellipsis);
        }
        return format;
    }

    bool create_devices() {
        if (d2d == nullptr &&
            FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                     reinterpret_cast<void**>(&d2d)))) {
            return false;
        }
        if (dwrite == nullptr &&
            FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(&dwrite)))) {
            return false;
        }
        if (ellipsis == nullptr && dwrite != nullptr) {
            dwrite->CreateEllipsisTrimmingSign(nullptr, &ellipsis);
        }
        if (fBody == nullptr) {
            fWord = make_format(L"Segoe UI Semibold", 17.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fHead = make_format(L"Segoe UI Semibold", 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fBody = make_format(L"Segoe UI", 13.0f, DWRITE_FONT_WEIGHT_NORMAL);
            fBold = make_format(L"Segoe UI Semibold", 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fSmall = make_format(L"Segoe UI", 11.5f, DWRITE_FONT_WEIGHT_NORMAL);
            fMono = make_format(L"Cascadia Mono", 12.0f, DWRITE_FONT_WEIGHT_NORMAL);
            if (fMono == nullptr) {
                fMono = make_format(L"Consolas", 12.0f, DWRITE_FONT_WEIGHT_NORMAL);
            }
            fMonoSmall = make_format(L"Cascadia Mono", 11.0f, DWRITE_FONT_WEIGHT_NORMAL);
            if (fMonoSmall == nullptr) {
                fMonoSmall = make_format(L"Consolas", 11.0f, DWRITE_FONT_WEIGHT_NORMAL);
            }
            fStat = make_format(L"Segoe UI Semibold", 21.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fWrap = make_format(L"Segoe UI", 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_WORD_WRAPPING_WRAP);
            fWrapBold = make_format(L"Segoe UI Semibold", 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                    DWRITE_WORD_WRAPPING_WRAP);
        }
        return fBody != nullptr;
    }

    bool ensure_target() {
        if (target != nullptr) {
            return true;
        }
        RECT client = {};
        GetClientRect(window, &client);
        D2D1_SIZE_U size = D2D1::SizeU(std::max<LONG>(1, client.right - client.left),
                                       std::max<LONG>(1, client.bottom - client.top));
        D2D1_RENDER_TARGET_PROPERTIES properties =
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE),
                                        static_cast<float>(dpi), static_cast<float>(dpi));
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProperties = D2D1::HwndRenderTargetProperties(window, size);
        if (FAILED(d2d->CreateHwndRenderTarget(properties, hwndProperties, &target))) {
            return false;
        }
        target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target->CreateSolidColorBrush(kText, &brush);
        build_bitmaps();
        return brush != nullptr;
    }

    ID2D1Bitmap* tinted_mask(const unsigned char* mask, const D2D1_COLOR_F& color) {
        std::vector<uint32_t> pixels(static_cast<size_t>(kGlyphSize) * static_cast<size_t>(kGlyphSize));
        uint32_t red = static_cast<uint32_t>(color.r * 255.0f + 0.5f);
        uint32_t green = static_cast<uint32_t>(color.g * 255.0f + 0.5f);
        uint32_t blue = static_cast<uint32_t>(color.b * 255.0f + 0.5f);
        for (size_t i = 0; i < pixels.size(); ++i) {
            uint32_t alpha = mask[i];
            uint32_t r = (red * alpha) / 255u;
            uint32_t g = (green * alpha) / 255u;
            uint32_t b = (blue * alpha) / 255u;
            pixels[i] = (alpha << 24) | (r << 16) | (g << 8) | b;
        }
        D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        ID2D1Bitmap* bitmap = nullptr;
        target->CreateBitmap(D2D1::SizeU(kGlyphSize, kGlyphSize), pixels.data(), kGlyphSize * 4u, properties,
                             &bitmap);
        return bitmap;
    }

    void build_bitmaps() {
        if (target == nullptr || logo != nullptr) {
            return;
        }
        D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        target->CreateBitmap(D2D1::SizeU(kLogoWidth, kLogoHeight), kLogoPixels, kLogoWidth * 4u, properties, &logo);
        glyphSearch = tinted_mask(kGlyphSearch, kDim);
        glyphSearchAmber = tinted_mask(kGlyphSearch, kAmber);
        glyphRefresh = tinted_mask(kGlyphRefresh, kDim);
        glyphDownload = tinted_mask(kGlyphDownload, kAmber);
        glyphEmpty = tinted_mask(kGlyphEmpty, kAmber);

        D2D1_GRADIENT_STOP stops[2] = {};
        stops[0] = {0.0f, rgb(0xF6C964)};
        stops[1] = {1.0f, rgb(0x8A5A16)};
        ID2D1GradientStopCollection* collection = nullptr;
        if (SUCCEEDED(target->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                                                            &collection))) {
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gradient = D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, 1.0f));
            target->CreateLinearGradientBrush(gradient, collection, &brushHistogram);
            collection->Release();
        }
    }

    void drop_target() {
        if (brushHistogram != nullptr) {
            brushHistogram->Release();
            brushHistogram = nullptr;
        }
        for (ID2D1Bitmap** bitmap : {&logo, &glyphSearch, &glyphSearchAmber, &glyphRefresh, &glyphDownload,
                                     &glyphEmpty}) {
            if (*bitmap != nullptr) {
                (*bitmap)->Release();
                *bitmap = nullptr;
            }
        }
        if (brush != nullptr) {
            brush->Release();
            brush = nullptr;
        }
        if (target != nullptr) {
            target->Release();
            target = nullptr;
        }
    }

    void fill(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color) {
        brush->SetColor(color);
        target->FillRectangle(rect, brush);
    }

    void fill_round(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color) {
        brush->SetColor(color);
        target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush);
    }

    void stroke(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float width = 1.0f) {
        brush->SetColor(color);
        target->DrawRectangle(rect, brush, width);
    }

    void stroke_line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& color, float width = 1.0f) {
        brush->SetColor(color);
        target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, width);
    }

    void text(IDWriteTextFormat* format, const std::wstring& value, const D2D1_RECT_F& rect,
              const D2D1_COLOR_F& color, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
              DWRITE_PARAGRAPH_ALIGNMENT para = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
        if (format == nullptr || value.empty()) {
            return;
        }
        brush->SetColor(color);
        format->SetTextAlignment(align);
        format->SetParagraphAlignment(para);
        target->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), format, rect, brush,
                          D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    }

    float text_width(IDWriteTextFormat* format, const std::wstring& value) {
        if (format == nullptr || value.empty()) {
            return 0.0f;
        }
        IDWriteTextLayout* layout = nullptr;
        if (FAILED(dwrite->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format, 4096.0f,
                                            200.0f, &layout))) {
            return 0.0f;
        }
        DWRITE_TEXT_METRICS metrics = {};
        layout->GetMetrics(&metrics);
        layout->Release();
        return metrics.width;
    }

    void compute_layout();
    void rebuild();
    void rebuild_buckets();
    bool hidden_by_filters(size_t index) const;
    void start_scan();
    void finish_scan();
    void render();
    void paint_top();
    void paint_summary();
    void paint_rail();
    void paint_table();
    void paint_drawer();
    void paint_status();
    void paint_scan_overlay();
    void paint_details(const Entry& entry, const SourceInfo* source, const D2D1_RECT_F& area);
    void on_mouse_move(float x, float y);
    void on_click(float x, float y);
    void on_wheel(float delta);
    void on_char(wchar_t ch);
    void on_key(int key);
    void export_csv();
    int entry_at(float y);
    int separator_at(float x, float y) const;
    void ensure_visible(int position);
    const Entry* selected_entry() const;
    const SourceInfo* source_of(const Entry& entry) const;
    std::wstring mode_text() const;
};

std::wstring day_part(const std::wstring& stamp) {
    if (stamp.size() >= 10) {
        return stamp.substr(0, 10);
    }
    return stamp;
}

std::wstring short_day(const std::wstring& day) {
    if (day.size() >= 10) {
        return day.substr(5);
    }
    return day;
}

bool contains_query(const std::wstring& haystack, const std::wstring& query) {
    return haystack.find(query) != std::wstring::npos;
}

bool binary_extension(const std::wstring& path) {
    static const wchar_t* kExtensions[] = {L".exe", L".com", L".scr", L".msi", L".dll", L".cpl",
                                           L".sys", L".ocx", L".bat", L".cmd", L".ps1"};
    std::wstring lowered = lower(path);
    for (const wchar_t* extension : kExtensions) {
        if (ends_with_ci(lowered, extension)) {
            return true;
        }
    }
    return false;
}

bool filesystem_path(const std::wstring& path) {
    if (path.size() < 3) {
        return false;
    }
    if (path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
        return true;
    }
    return path[0] == L'\\' && path[1] == L'\\';
}

void App::compute_layout() {
    RECT client = {};
    GetClientRect(window, &client);
    float width = static_cast<float>(client.right - client.left) / scale;
    float height = static_cast<float>(client.bottom - client.top) / scale;

    float topH = 64.0f;
    float summaryH = 78.0f;
    float statusH = 26.0f;
    float railW = 264.0f;
    float drawerW = 396.0f;

    layout.top = D2D1::RectF(0.0f, 0.0f, width, topH);
    layout.summary = D2D1::RectF(0.0f, topH, width, topH + summaryH);
    layout.status = D2D1::RectF(0.0f, height - statusH, width, height);

    float searchW = std::min(340.0f, std::max(180.0f, width - 800.0f));
    layout.search = D2D1::RectF(248.0f, 17.0f, 248.0f + searchW, 47.0f);
    layout.exportButton = D2D1::RectF(width - 122.0f, 17.0f, width - 24.0f, 47.0f);
    layout.rescan = D2D1::RectF(width - 236.0f, 17.0f, width - 134.0f, 47.0f);

    bool hasDrawer = selected >= 0;
    float tableRight = width - (hasDrawer ? drawerW : 0.0f);
    layout.rail = D2D1::RectF(0.0f, topH + summaryH, railW, height - statusH);
    layout.table = D2D1::RectF(railW, topH + summaryH, tableRight, height - statusH);
    layout.drawer = D2D1::RectF(tableRight, topH + summaryH, width, height - statusH);

    layout.modeEverywhere = D2D1::RectF(16.0f, layout.rail.top + 42.0f, railW * 0.5f - 2.0f,
                                        layout.rail.top + 66.0f);
    layout.modeStandard = D2D1::RectF(railW * 0.5f + 2.0f, layout.rail.top + 42.0f, railW - 16.0f,
                                      layout.rail.top + 66.0f);
    layout.filterBoot = D2D1::RectF(16.0f, layout.rail.top + 96.0f, railW - 16.0f, layout.rail.top + 116.0f);
    layout.filterNoise = D2D1::RectF(16.0f, layout.rail.top + 122.0f, railW - 16.0f, layout.rail.top + 142.0f);
    layout.railList = D2D1::RectF(0.0f, layout.rail.top + 166.0f, railW, layout.rail.bottom);

    layout.header = D2D1::RectF(layout.table.left, layout.table.top, layout.table.right,
                                layout.table.top + 34.0f);
    float tableWidth = layout.table.right - layout.table.left;
    float colPin = 34.0f;
    float colNum = 62.0f;
    float avail = std::max(240.0f, tableWidth - colPin - colNum);
    if (colAppW < 0.0f) {
        colAppW = std::max(100.0f, std::min(196.0f, avail * 0.24f));
    }
    colAppW = std::min(std::max(colAppW, 90.0f), avail - 120.0f - 46.0f - 150.0f);
    if (colCountW < 0.0f) {
        colCountW = 74.0f;
    }
    colCountW = std::min(std::max(colCountW, 46.0f), avail - colAppW - 120.0f - 150.0f);
    if (colTargetW < 0.0f) {
        colTargetW = avail - colAppW - colCountW;
    }
    colTargetW = std::min(std::max(colTargetW, 120.0f), avail - colAppW - colCountW - 150.0f);
    layout.colPin = layout.table.left;
    layout.colNum = layout.colPin + colPin;
    layout.colApp = layout.colNum + colNum;
    layout.colTarget = layout.colApp + colAppW;
    layout.colCount = layout.colTarget + colTargetW;
    layout.colModified = layout.colCount + colCountW;

    float statsRight = layout.table.right;
    float statsLeft = layout.rail.right + 24.0f;
    float statsWidth = 118.0f * 3.0f + 300.0f;
    float histogramLeft = statsLeft + statsWidth + 26.0f;
    float histogramRight = statsRight - 24.0f;
    if (histogramRight - histogramLeft >= 160.0f) {
        layout.histogram = D2D1::RectF(histogramLeft, layout.summary.top + 18.0f, histogramRight,
                                       layout.summary.bottom - 16.0f);
    } else {
        layout.histogram = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
    }
}

bool App::hidden_by_filters(size_t index) const {
    if (index >= dataset.entries.size()) {
        return false;
    }
    const Entry& entry = dataset.entries[index];
    if (bootOnly) {
        double time = entry.lastModified > 0.0 ? entry.lastModified : entry.entryCreated;
        if (!(time >= bootTime)) {
            return true;
        }
    }
    if (reduceNoise && index < noise.size() && noise[index] != 0) {
        return true;
    }
    return false;
}

void App::rebuild_buckets() {
    buckets.clear();
    std::vector<std::pair<std::wstring, int>> counts;
    for (size_t index = 0; index < dataset.entries.size(); ++index) {
        const Entry& entry = dataset.entries[index];
        if (hidden_by_filters(index)) {
            continue;
        }
        if (sourceFilter >= 0 && entry.sourceIndex != sourceFilter) {
            continue;
        }
        if (!query.empty() && !contains_query(entry.search, query)) {
            continue;
        }
        double time = entry.lastModified > 0.0 ? entry.lastModified : entry.entryCreated;
        if (!(time > 0.0)) {
            continue;
        }
        std::wstring day = day_part(format_time(time, false));
        size_t slot = 0;
        for (; slot < counts.size(); ++slot) {
            if (counts[slot].first == day) {
                break;
            }
        }
        if (slot == counts.size()) {
            counts.push_back({day, 0});
        }
        ++counts[slot].second;
    }
    std::sort(counts.begin(), counts.end(),
              [](const std::pair<std::wstring, int>& a, const std::pair<std::wstring, int>& b) {
                  return a.first < b.first;
              });
    for (const std::pair<std::wstring, int>& item : counts) {
        Bucket bucket;
        bucket.day = item.first;
        bucket.label = short_day(item.first);
        bucket.count = item.second;
        buckets.push_back(bucket);
    }
}

void App::rebuild() {
    rebuild_buckets();
    if (!dayFilter.empty()) {
        bool present = false;
        for (const Bucket& bucket : buckets) {
            if (bucket.day == dayFilter) {
                present = true;
                break;
            }
        }
        if (!present) {
            dayFilter.clear();
        }
    }

    sourceCounts.assign(dataset.sources.size(), 0);
    visible.clear();
    hiddenFilters = 0;
    noiseHidden = 0;
    for (size_t index = 0; index < dataset.entries.size(); ++index) {
        const Entry& entry = dataset.entries[index];
        if (reduceNoise && index < noise.size() && noise[index] != 0) {
            ++noiseHidden;
            ++hiddenFilters;
            continue;
        }
        if (bootOnly) {
            double time = entry.lastModified > 0.0 ? entry.lastModified : entry.entryCreated;
            if (!(time >= bootTime)) {
                ++hiddenFilters;
                continue;
            }
        }
        if (!dayFilter.empty()) {
            double time = entry.lastModified > 0.0 ? entry.lastModified : entry.entryCreated;
            if (!(time > 0.0) || day_part(format_time(time, false)) != dayFilter) {
                continue;
            }
        }
        if (!query.empty() && !contains_query(entry.search, query)) {
            continue;
        }
        if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(sourceCounts.size())) {
            ++sourceCounts[static_cast<size_t>(entry.sourceIndex)];
        }
        if (sourceFilter >= 0 && entry.sourceIndex != sourceFilter) {
            continue;
        }
        visible.push_back(static_cast<int>(index));
    }

    railOrder.clear();
    for (size_t i = 0; i < dataset.sources.size(); ++i) {
        railOrder.push_back(static_cast<int>(i));
    }
    std::stable_sort(railOrder.begin(), railOrder.end(), [this](int a, int b) {
        int left = sourceCounts[static_cast<size_t>(a)];
        int right = sourceCounts[static_cast<size_t>(b)];
        if (left != right) {
            return left > right;
        }
        return lower(dataset.sources[static_cast<size_t>(a)].appName) <
               lower(dataset.sources[static_cast<size_t>(b)].appName);
    });
    std::stable_sort(visible.begin(), visible.end(), [this](int a, int b) {
        const Entry& left = dataset.entries[static_cast<size_t>(a)];
        const Entry& right = dataset.entries[static_cast<size_t>(b)];
        auto byTime = [](const Entry& x) {
            return x.lastModified > 0.0 ? x.lastModified : (x.entryCreated > 0.0 ? x.entryCreated : 0.0);
        };
        switch (sortColumn) {
            case SortColumn::App: {
                std::wstring la = lower(left.appName);
                std::wstring lb = lower(right.appName);
                if (la != lb) {
                    return sortAscending ? la < lb : la > lb;
                }
                return byTime(left) > byTime(right);
            }
            case SortColumn::Target: {
                std::wstring la = lower(left.target);
                std::wstring lb = lower(right.target);
                if (la != lb) {
                    return sortAscending ? la < lb : la > lb;
                }
                return byTime(left) > byTime(right);
            }
            case SortColumn::Count: {
                if (left.interactions != right.interactions) {
                    return sortAscending ? left.interactions < right.interactions
                                         : left.interactions > right.interactions;
                }
                return byTime(left) > byTime(right);
            }
            case SortColumn::Modified:
            default: {
                double ta = byTime(left);
                double tb = byTime(right);
                if (ta != tb) {
                    return sortAscending ? ta < tb : ta > tb;
                }
                return lower(left.target) < lower(right.target);
            }
        }
    });
    scroll = std::min(scroll, std::max(0.0f, static_cast<float>(visible.size()) * layout.rowHeight -
                                                 (layout.table.bottom - layout.header.bottom)));
    if (scroll < 0.0f) {
        scroll = 0.0f;
    }
}

const SourceInfo* App::source_of(const Entry& entry) const {
    if (entry.sourceIndex >= 0 && static_cast<size_t>(entry.sourceIndex) < dataset.sources.size()) {
        return &dataset.sources[static_cast<size_t>(entry.sourceIndex)];
    }
    return nullptr;
}

const Entry* App::selected_entry() const {
    if (selected < 0) {
        return nullptr;
    }
    for (size_t i = 0; i < dataset.entries.size(); ++i) {
        if (dataset.entries[i].id == selected) {
            return &dataset.entries[i];
        }
    }
    return nullptr;
}

std::wstring App::mode_text() const {
    return everywhere ? L"every volume" : L"standard folders";
}

void App::start_scan() {
    if (scanning) {
        return;
    }
    if (worker.joinable()) {
        worker.join();
    }
    scanning = true;
    done = false;
    selected = -1;
    scroll = 0.0f;
    hoverRow = -1;
    {
        std::lock_guard<std::mutex> guard(doneMutex);
        finished.reset();
        finishedNoise.reset();
    }
    dataset = Dataset();
    noise.clear();
    noiseHidden = 0;
    rebuild_buckets();
    rebuild();
    bool all = everywhere;
    worker = std::thread([this, all]() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto result = std::make_unique<Dataset>();
        run_scan(*result, scan, all);
        auto flags = std::make_unique<std::vector<uint8_t>>(result->entries.size(), 0);
        for (size_t i = 0; i < result->entries.size(); ++i) {
            const Entry& entry = result->entries[i];
            if (!filesystem_path(entry.target) || !binary_extension(entry.target)) {
                continue;
            }
            if (microsoft_signed(entry.target)) {
                (*flags)[i] = 1;
            }
        }
        {
            std::lock_guard<std::mutex> guard(doneMutex);
            finished = std::move(result);
            finishedNoise = std::move(flags);
        }
        done = true;
        CoUninitialize();
    });
}

void App::finish_scan() {
    {
        std::lock_guard<std::mutex> guard(doneMutex);
        if (finished != nullptr) {
            dataset = std::move(*finished);
            finished.reset();
        }
        if (finishedNoise != nullptr) {
            noise = std::move(*finishedNoise);
            finishedNoise.reset();
        }
    }
    if (noise.size() != dataset.entries.size()) {
        noise.assign(dataset.entries.size(), 0);
    }
    if (worker.joinable()) {
        worker.join();
    }
    scanning = false;
    rebuild_buckets();
    rebuild();
    scroll = 0.0f;
    InvalidateRect(window, nullptr, FALSE);
}

int App::separator_at(float x, float y) const {
    if (y < layout.header.top || y > layout.header.bottom) {
        return -1;
    }
    if (std::fabs(x - layout.colTarget) <= 5.0f) {
        return 0;
    }
    if (std::fabs(x - layout.colCount) <= 5.0f) {
        return 1;
    }
    if (std::fabs(x - layout.colModified) <= 5.0f) {
        return 2;
    }
    return -1;
}

int App::entry_at(float y) {
    if (y < layout.header.bottom) {
        return -1;
    }
    float offset = y - layout.header.bottom + scroll;
    if (offset < 0.0f) {
        return -1;
    }
    int position = static_cast<int>(offset / layout.rowHeight);
    if (position < 0 || position >= static_cast<int>(visible.size())) {
        return -1;
    }
    return position;
}

void App::ensure_visible(int position) {
    if (position < 0) {
        return;
    }
    float top = static_cast<float>(position) * layout.rowHeight;
    float bottom = top + layout.rowHeight;
    float viewTop = scroll;
    float viewBottom = scroll + (layout.table.bottom - layout.header.bottom);
    if (top < viewTop) {
        scroll = top;
    } else if (bottom > viewBottom) {
        scroll = bottom - (layout.table.bottom - layout.header.bottom);
    }
    scroll = std::max(0.0f, scroll);
}

void App::paint_top() {
    const D2D1_RECT_F& top = layout.top;
    fill(top, kBg);
    stroke_line(top.left, top.bottom - 0.5f, top.right, top.bottom - 0.5f, kHair);

    if (logo != nullptr) {
        D2D1_RECT_F icon = D2D1::RectF(22.0f, 15.0f, 56.0f, 49.0f);
        target->DrawBitmap(logo, icon, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    text(fWord, kAppName, D2D1::RectF(64.0f, 12.0f, 220.0f, 36.0f), kText);
    float wordW = text_width(fWord, kAppName);
    text(fWord, L".", D2D1::RectF(64.0f + wordW + 1.5f, 12.0f, 64.0f + wordW + 14.0f, 36.0f), kAmber);
    text(fSmall, L"jump list parser", D2D1::RectF(64.0f, 34.0f, 220.0f, 50.0f), kFaint);

    bool focus = searchFocused;
    fill_round(layout.search, 9.0f, focus ? kRaised : kPanel);
    if (focus) {
        brush->SetColor(rgb(0xE8A33C, 0.18f));
        target->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(layout.search.left - 2.0f, layout.search.top - 2.0f,
                                          layout.search.right + 2.0f, layout.search.bottom + 2.0f),
                              11.0f, 11.0f),
            brush, 2.0f);
    }
    brush->SetColor(focus ? kAmber : kHair);
    target->DrawRoundedRectangle(D2D1::RoundedRect(layout.search, 9.0f, 9.0f), brush, 1.0f);
    ID2D1Bitmap* magnifier = focus ? glyphSearchAmber : glyphSearch;
    if (magnifier != nullptr) {
        float iconSize = 15.0f;
        float iconY = (layout.search.top + layout.search.bottom - iconSize) * 0.5f;
        D2D1_RECT_F iconRect = D2D1::RectF(layout.search.left + 10.0f, iconY,
                                           layout.search.left + 10.0f + iconSize, iconY + iconSize);
        target->DrawBitmap(magnifier, iconRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    std::wstring display = query;
    D2D1_RECT_F textRect = D2D1::RectF(layout.search.left + 31.0f, layout.search.top + 5.0f,
                                       layout.search.right - 14.0f, layout.search.bottom - 5.0f);
    if (display.empty() && !focus) {
        text(fBody, L"Search targets, hosts, AppIDs   (Ctrl+F)", textRect, kFaint);
    } else {
        text(fBody, display, textRect, kText);
    }

    auto button = [this](const D2D1_RECT_F& rect, const wchar_t* label, bool primary, bool hovered, bool enabled,
                         ID2D1Bitmap* glyph) {
        D2D1_COLOR_F bg = primary ? (hovered ? rgb(0x2C2312) : rgb(0x262011)) : (hovered ? kRaised : kBg);
        fill_round(rect, 8.0f, bg);
        brush->SetColor(primary ? rgb(0xE8A33C, hovered ? 0.75f : 0.45f) : (hovered ? kAmber : kHair));
        target->DrawRoundedRectangle(D2D1::RoundedRect(rect, 8.0f, 8.0f), brush, 1.0f);
        if (glyph != nullptr) {
            float iconSize = 15.0f;
            float iconY = (rect.top + rect.bottom - iconSize) * 0.5f;
            D2D1_RECT_F iconRect = D2D1::RectF(rect.left + 12.0f, iconY, rect.left + 12.0f + iconSize,
                                               iconY + iconSize);
            target->DrawBitmap(glyph, iconRect, enabled ? 1.0f : 0.35f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        text(fHead, label, D2D1::RectF(rect.left + 30.0f, rect.top, rect.right - 4.0f, rect.bottom),
             enabled ? (primary ? rgb(0xF8D98E) : (hovered ? kText : kDim)) : kFaint,
             DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    button(layout.rescan, scanning ? L"Scanning" : L"Rescan", false, hoverRow == -2, !scanning, glyphRefresh);
    button(layout.exportButton, L"Export CSV", true, hoverRow == -3, !dataset.entries.empty(), glyphDownload);
}

void App::paint_summary() {
    const D2D1_RECT_F& summary = layout.summary;
    fill(summary, kPanel);
    stroke_line(summary.left, summary.bottom - 0.5f, summary.right, summary.bottom - 0.5f, kHair);

    float x = layout.rail.right + 24.0f;
    float labelY = summary.top + 16.0f;
    float valueY = summary.top + 32.0f;

    struct Stat {
        std::wstring label;
        std::wstring value;
    };
    std::vector<Stat> stats;
    stats.push_back({L"JUMP LISTS", format_int(static_cast<long long>(dataset.sources.size()))});
    stats.push_back({L"ENTRIES", format_int(static_cast<long long>(dataset.entries.size()))});
    stats.push_back({L"PINNED", format_int(dataset.pinned)});
    std::wstring range = dataset.rangeFrom;
    if (!dataset.rangeTo.empty() && dataset.rangeTo != L"--") {
        range = dataset.rangeFrom + L"  ->  " + dataset.rangeTo;
    }
    stats.push_back({L"RANGE", range.empty() ? L"--" : range});

    for (size_t i = 0; i < stats.size(); ++i) {
        const Stat& stat = stats[i];
        bool wide = i == 3;
        float blockW = wide ? 300.0f : 104.0f;
        text(fSmall, stat.label, D2D1::RectF(x, labelY, x + blockW, labelY + 14.0f), kFaint);
        text(wide ? fBody : fStat, stat.value,
             D2D1::RectF(x, valueY, x + blockW, valueY + 26.0f), i == 2 && dataset.pinned > 0 ? kAmber : kText);
        if (!wide) {
            stroke_line(x + blockW + 6.0f, summary.top + 22.0f, x + blockW + 6.0f, summary.bottom - 22.0f, kHair);
        }
        x += blockW + (wide ? 0.0f : 14.0f);
    }

    if (buckets.empty()) {
        return;
    }
    if (layout.histogram.right - layout.histogram.left < 160.0f) {
        return;
    }
    float left = layout.histogram.left;
    float right = layout.histogram.right;
    float top = layout.histogram.top;
    float bottom = layout.histogram.bottom;
    int maxCount = 1;
    for (const Bucket& bucket : buckets) {
        maxCount = std::max(maxCount, bucket.count);
    }
    int count = static_cast<int>(buckets.size());
    float slot = (right - left) / static_cast<float>(count);
    float barW = std::min(16.0f, std::max(2.0f, slot - 2.0f));
    for (int i = 0; i < count; ++i) {
        const Bucket& bucket = buckets[i];
        float height = (bottom - top) * (static_cast<float>(bucket.count) / static_cast<float>(maxCount));
        height = std::max(2.0f, height);
        float bx = left + slot * static_cast<float>(i) + (slot - barW) * 0.5f;
        bool active = !dayFilter.empty() && dayFilter == bucket.day;
        bool hot = hoverBucket == i;
        D2D1_RECT_F barRect = D2D1::RectF(bx, bottom - height, bx + barW, bottom);
        if (active) {
            fill_round(barRect, 2.0f, kAmber);
        } else if (hot) {
            fill_round(barRect, 2.0f, rgb(0xE8A33C, 0.55f));
        } else if (brushHistogram != nullptr) {
            brushHistogram->SetTransform(D2D1::Matrix3x2F::Translation(bx, bottom - height) *
                                         D2D1::Matrix3x2F::Scale(1.0f, std::max(1.0f, height)));
            target->FillRoundedRectangle(D2D1::RoundedRect(barRect, 2.0f, 2.0f), brushHistogram);
        } else {
            fill(barRect, rgb(0x3A4245));
        }
    }
    if (brushHistogram != nullptr) {
        brushHistogram->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    if (hoverBucket >= 0 && hoverBucket < count) {
        const Bucket& bucket = buckets[static_cast<size_t>(hoverBucket)];
        std::wstring label = bucket.day + L"   " + format_int(bucket.count) + L" entries";
        float width = text_width(fSmall, label) + 18.0f;
        float slotCenter = left + slot * static_cast<float>(hoverBucket) + slot * 0.5f;
        float bx = std::min(std::max(slotCenter - width * 0.5f, left), right - width);
        D2D1_RECT_F bubble = D2D1::RectF(bx, top - 12.0f, bx + width, top + 4.0f);
        fill_round(bubble, 4.0f, kRaised);
        brush->SetColor(kHair);
        target->DrawRoundedRectangle(D2D1::RoundedRect(bubble, 4.0f, 4.0f), brush, 1.0f);
        text(fSmall, label, D2D1::RectF(bubble.left + 9.0f, bubble.top, bubble.right - 9.0f, bubble.bottom), kText);
    }
}

void App::paint_rail() {
    const D2D1_RECT_F& rail = layout.rail;
    fill(rail, kBg);
    stroke_line(rail.right - 0.5f, rail.top, rail.right - 0.5f, rail.bottom, kHair);

    text(fSmall, L"SOURCE", D2D1::RectF(16.0f, rail.top + 18.0f, 120.0f, rail.top + 34.0f), kFaint);

    auto segment = [this](const D2D1_RECT_F& rect, const wchar_t* label, bool active, bool hovered) {
        if (active) {
            fill_round(rect, 6.0f, kRaised);
        } else if (hovered) {
            fill_round(rect, 6.0f, rgb(0x141819));
        }
        text(fSmall, label, rect, active ? kText : kDim, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    segment(layout.modeEverywhere, L"All volumes", everywhere, hoverMode == 0);
    segment(layout.modeStandard, L"Standard", !everywhere, hoverMode == 1);

    text(fSmall, L"FILTERS", D2D1::RectF(16.0f, rail.top + 78.0f, 120.0f, rail.top + 94.0f), kFaint);

    auto toggle = [this](const D2D1_RECT_F& rect, const wchar_t* label, bool on, bool hot,
                         const std::wstring& hint) {
        if (hot) {
            fill_round(D2D1::RectF(rect.left - 10.0f, rect.top, rect.right, rect.bottom), 6.0f, rgb(0x121617));
        }
        float mid = (rect.top + rect.bottom) * 0.5f;
        D2D1_RECT_F box = D2D1::RectF(rect.left, mid - 7.0f, rect.left + 14.0f, mid + 7.0f);
        if (on) {
            fill_round(box, 3.0f, kAmber);
            brush->SetColor(kBg);
            target->DrawLine(D2D1::Point2F(box.left + 3.4f, mid), D2D1::Point2F(box.left + 5.9f, mid + 2.6f),
                             brush, 1.7f);
            target->DrawLine(D2D1::Point2F(box.left + 5.9f, mid + 2.6f), D2D1::Point2F(box.left + 10.6f, mid - 3.2f),
                             brush, 1.7f);
        } else {
            brush->SetColor(hot ? kDim : kFaint);
            target->DrawRoundedRectangle(D2D1::RoundedRect(box, 3.0f, 3.0f), brush, 1.0f);
        }
        float hintWidth = hint.empty() ? 0.0f : text_width(fMonoSmall, hint) + 10.0f;
        text(fBody, label, D2D1::RectF(box.right + 9.0f, rect.top, rect.right - hintWidth, rect.bottom),
             on ? kText : (hot ? kDim : rgb(0x848D90)));
        if (!hint.empty()) {
            text(fMonoSmall, hint, D2D1::RectF(box.right + 9.0f, rect.top, rect.right, rect.bottom), kFaint,
                 DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    };

    std::wstring bootHint;
    if (bootTime > 0.0) {
        std::wstring stamp = format_time(bootTime, false);
        if (stamp.size() >= 16) {
            bootHint = L"since " + stamp.substr(11, 5);
        }
    }
    std::wstring noiseHint = reduceNoise && noiseHidden > 0
                                 ? format_int(static_cast<long long>(noiseHidden)) + L" hidden"
                                 : std::wstring();
    toggle(layout.filterBoot, L"In instance only", bootOnly, hoverFilter == 0, bootHint);
    toggle(layout.filterNoise, L"Reduce noise", reduceNoise, hoverFilter == 1, noiseHint);

    float y = layout.railList.top;
    struct Row {
        std::wstring title;
        std::wstring count;
        int filter = -1;
        bool all = false;
    };
    int total = 0;
    for (int count : sourceCounts) {
        total += count;
    }
    std::vector<Row> rows;
    rows.push_back({L"All sources", format_int(total), -1, true});
    std::vector<int> order = railOrder;
    for (int index : order) {
        const SourceInfo& source = dataset.sources[static_cast<size_t>(index)];
        int count = static_cast<size_t>(index) < sourceCounts.size()
                        ? sourceCounts[static_cast<size_t>(index)]
                        : source.entryCount;
        rows.push_back({source.appName, format_int(count), index, false});
    }

    float itemH = 42.0f;
    int hoverItem = -1;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (y + itemH > rail.bottom - 6.0f) {
            break;
        }
        const Row& row = rows[i];
        bool active = row.filter == sourceFilter;
        bool hot = hoverSource == static_cast<int>(i);
        if (active) {
            fill(D2D1::RectF(rail.left, y, rail.right - 1.0f, y + itemH), kSelect);
            fill(D2D1::RectF(rail.left, y, rail.left + 2.0f, y + itemH), kAmber);
        } else if (hot) {
            fill(D2D1::RectF(rail.left, y, rail.right - 1.0f, y + itemH), rgb(0x121617));
        }
        std::wstring title = row.title;
        std::wstring count = row.count;
        text(fBody, title, D2D1::RectF(16.0f, y + 2.0f, rail.right - 62.0f, y + itemH - 2.0f),
             active ? kText : (hot ? kText : kDim));
        text(fMonoSmall, count, D2D1::RectF(rail.right - 62.0f, y + 2.0f, rail.right - 14.0f, y + itemH - 2.0f),
             active ? kAmber : kFaint, DWRITE_TEXT_ALIGNMENT_TRAILING);
        y += itemH;
    }
}

void App::paint_table() {
    const D2D1_RECT_F& table = layout.table;
    fill(table, kBg);

    const D2D1_RECT_F& header = layout.header;
    fill(header, kPanel);
    stroke_line(header.left, header.bottom - 0.5f, header.right, header.bottom - 0.5f, kHair);

    struct Column {
        const wchar_t* label;
        float left;
        float right;
        SortColumn sort;
        DWRITE_TEXT_ALIGNMENT align;
    };
    std::vector<Column> columns;
    columns.push_back({L"", layout.colPin, layout.colNum, SortColumn::Modified, DWRITE_TEXT_ALIGNMENT_LEADING});
    columns.push_back({L"#", layout.colNum, layout.colApp, SortColumn::Modified, DWRITE_TEXT_ALIGNMENT_TRAILING});
    columns.push_back({L"APPLICATION", layout.colApp, layout.colTarget, SortColumn::App,
                       DWRITE_TEXT_ALIGNMENT_LEADING});
    columns.push_back({L"TARGET", layout.colTarget, layout.colCount, SortColumn::Target,
                       DWRITE_TEXT_ALIGNMENT_LEADING});
    columns.push_back({L"USES", layout.colCount, layout.colModified, SortColumn::Count,
                       DWRITE_TEXT_ALIGNMENT_TRAILING});
    columns.push_back({L"LAST USED", layout.colModified, table.right - 16.0f, SortColumn::Modified,
                       DWRITE_TEXT_ALIGNMENT_LEADING});

    for (size_t i = 0; i < columns.size(); ++i) {
        const Column& column = columns[i];
        std::wstring label = column.label;
        bool active = sortColumn == column.sort && i > 0;
        if (active) {
            label += sortAscending ? L"  ^" : L"  v";
        }
        float padLeft = column.align == DWRITE_TEXT_ALIGNMENT_TRAILING ? 4.0f : (i == 0 ? 20.0f : 12.0f);
        text(fSmall, label,
             D2D1::RectF(column.left + padLeft, header.top, column.right - 6.0f, header.bottom),
             active ? kText : kFaint, column.align);
    }

    auto separator = [&](float x, int index) {
        bool active = hoverSep == index || dragSep == index;
        brush->SetColor(active ? kAmber : kHair);
        target->DrawLine(D2D1::Point2F(x, header.top + 9.0f), D2D1::Point2F(x, header.bottom - 9.0f), brush,
                         active ? 1.5f : 1.0f);
    };
    separator(layout.colTarget, 0);
    separator(layout.colCount, 1);
    separator(layout.colModified, 2);

    target->PushAxisAlignedClip(D2D1::RectF(table.left, header.bottom, table.right, table.bottom),
                                D2D1_ANTIALIAS_MODE_ALIASED);

    int first = std::max(0, static_cast<int>(scroll / layout.rowHeight));
    float offsetY = layout.header.bottom - std::fmod(scroll, layout.rowHeight);
    int count = static_cast<int>(visible.size());
    for (int position = first; position < count; ++position) {
        float y = offsetY + static_cast<float>(position - first) * layout.rowHeight;
        if (y >= table.bottom) {
            break;
        }
        const Entry& entry = dataset.entries[static_cast<size_t>(visible[static_cast<size_t>(position)])];
        bool isSelected = selected >= 0 && entry.id == selected;
        bool hot = hoverRow == position;
        if (isSelected) {
            fill(D2D1::RectF(table.left, y, table.right, y + layout.rowHeight), kSelect);
            fill(D2D1::RectF(table.left, y, table.left + 2.0f, y + layout.rowHeight), kAmber);
        } else if (hot) {
            fill(D2D1::RectF(table.left, y, table.right, y + layout.rowHeight), rgb(0x111516));
        }
        stroke_line(table.left + 20.0f, y + layout.rowHeight - 0.5f, table.right, y + layout.rowHeight - 0.5f,
                    isSelected ? rgb(0x2A3133) : rgb(0x1A1F21));

        float dotX = layout.colPin + 26.0f;
        float dotY = y + layout.rowHeight * 0.5f;
        if (entry.pinned) {
            brush->SetColor(kAmber);
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), 3.4f, 3.4f), brush);
        } else {
            brush->SetColor(rgb(0x39423F));
            target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), 3.2f, 3.2f), brush, 1.0f);
        }

        text(fMonoSmall, format_int(entry.id),
             D2D1::RectF(layout.colNum, y, layout.colApp - 10.0f, y + layout.rowHeight),
             isSelected ? kText : kFaint, DWRITE_TEXT_ALIGNMENT_TRAILING);

        float tileX = layout.colApp + 12.0f;
        float tileY = y + 14.0f;
        fill_round(D2D1::RectF(tileX, tileY, tileX + 28.0f, tileY + 28.0f), 6.0f, rgb(0x1A2021));
        std::wstring initial = entry.appName.empty() ? L"?" : entry.appName.substr(0, 1);
        text(fHead, lower(initial), D2D1::RectF(tileX, tileY, tileX + 28.0f, tileY + 28.0f), kDim,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        text(fBody, entry.appName, D2D1::RectF(tileX + 40.0f, y + 11.0f, layout.colTarget - 12.0f, y + 31.0f),
             isSelected ? kText : rgb(0xD5D6D1));
        std::wstring kind = entry.automatic ? L"automatic" : L"custom";
        if (!entry.category.empty() && !entry.automatic) {
            kind = entry.category;
        }
        text(fSmall, kind, D2D1::RectF(tileX + 40.0f, y + 30.0f, layout.colTarget - 12.0f, y + 46.0f), kFaint);

        std::wstring target = entry.target;
        std::wstring tail = base_name_of(target);
        std::wstring parent = parent_of(target);
        if (tail.empty()) {
            tail = entry.category.empty() ? L"(no target)" : entry.category;
            parent.clear();
        }
        text(fBody, tail, D2D1::RectF(layout.colTarget + 12.0f, y + 11.0f, layout.colCount - 8.0f, y + 31.0f),
             isSelected ? kText : rgb(0xDDDCD6));
        if (!parent.empty()) {
            text(fSmall, parent, D2D1::RectF(layout.colTarget + 12.0f, y + 30.0f, layout.colCount - 8.0f, y + 46.0f),
                 kFaint);
        }

        std::wstring uses = entry.interactions > 0 ? format_int(entry.interactions) : L"-";
        text(fMono, uses, D2D1::RectF(layout.colCount, y, layout.colModified - 14.0f, y + layout.rowHeight),
             isSelected ? kAmber : kDim, DWRITE_TEXT_ALIGNMENT_TRAILING);

        double stamp = entry.lastModified > 0.0 ? entry.lastModified : entry.entryCreated;
        if (stamp > 0.0) {
            text(fMonoSmall, format_time(stamp, false),
                 D2D1::RectF(layout.colModified + 12.0f, y + 11.0f, table.right - 16.0f, y + 31.0f), kText);
            text(fSmall, format_relative(stamp, now_seconds()),
                 D2D1::RectF(layout.colModified + 12.0f, y + 30.0f, table.right - 16.0f, y + 46.0f), kFaint);
        } else {
            text(fMonoSmall, L"--", D2D1::RectF(layout.colModified + 12.0f, y, table.right - 16.0f, y + layout.rowHeight),
                 kFaint);
        }
    }
    target->PopAxisAlignedClip();

    if (visible.empty() && !scanning) {
        std::wstring title = dataset.sources.empty() ? L"No jump lists found" : L"Nothing matches this filter";
        std::wstring hint = dataset.sources.empty()
                                ? L"Nothing on this machine matched the jump list file names."
                                : L"Clear the search or pick another source.";
        float centerY = (layout.header.bottom + table.bottom) * 0.5f;
        if (glyphEmpty != nullptr) {
            float iconSize = 54.0f;
            D2D1_RECT_F iconRect = D2D1::RectF(table.left + (table.right - table.left - iconSize) * 0.5f,
                                               centerY - 72.0f,
                                               table.left + (table.right - table.left + iconSize) * 0.5f,
                                               centerY - 72.0f + iconSize);
            target->DrawBitmap(glyphEmpty, iconRect, 0.30f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        text(fBold, title, D2D1::RectF(table.left, centerY - 12.0f, table.right, centerY + 10.0f), kText,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        text(fSmall, hint, D2D1::RectF(table.left, centerY + 14.0f, table.right, centerY + 32.0f), kFaint,
             DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void App::paint_details(const Entry& entry, const SourceInfo* source, const D2D1_RECT_F& area) {
    float x = area.left + 22.0f;
    float width = area.right - x - 22.0f;
    float y = area.top + 20.0f;

    std::wstring title = entry.appName;
    text(fBold, title, D2D1::RectF(x, y, x + width, y + 24.0f), kText);
    if (entry.pinned) {
        std::wstring pin = L"PINNED";
        float chipW = text_width(fSmall, pin) + 18.0f;
        D2D1_RECT_F chip = D2D1::RectF(x + width - chipW, y + 2.0f, x + width, y + 20.0f);
        brush->SetColor(kAmberSoft);
        target->FillRoundedRectangle(D2D1::RoundedRect(chip, 4.0f, 4.0f), brush);
        brush->SetColor(kAmber);
        target->DrawRoundedRectangle(D2D1::RoundedRect(chip, 4.0f, 4.0f), brush, 1.0f);
        text(fSmall, pin, chip, kAmber, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    y += 26.0f;
    std::wstring subtitle = entry.automatic ? L"automatic destinations" : L"custom destinations";
    if (!entry.category.empty()) {
        subtitle += L"  /  " + entry.category;
    }
    text(fSmall, subtitle, D2D1::RectF(x, y, x + width, y + 16.0f), kFaint);
    y += 26.0f;
    stroke_line(x, y, x + width, y, kHair);
    y += 14.0f;

    auto formatted = [](double value) { return value > 0.0 ? format_time(value, true) : std::wstring(); };
    auto relative = [](double value) {
        return value > 0.0 ? format_relative(value, now_seconds()) : std::wstring();
    };

    auto section = [&](const wchar_t* label) {
        text(fSmall, label, D2D1::RectF(x, y, x + width, y + 16.0f), kAmber);
        y += 22.0f;
    };

    auto row = [&](const wchar_t* label, const std::wstring& value) {
        if (value.empty()) {
            return;
        }
        IDWriteTextLayout* layout = nullptr;
        if (FAILED(dwrite->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), fWrap, width, 600.0f,
                                            &layout))) {
            return;
        }
        DWRITE_TEXT_METRICS metrics = {};
        layout->GetMetrics(&metrics);
        float height = std::max(18.0f, metrics.height);
        text(fSmall, label, D2D1::RectF(x, y, x + 92.0f, y + height), kFaint, DWRITE_TEXT_ALIGNMENT_LEADING,
             DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        brush->SetColor(rgb(0xE2E0DA));
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        target->DrawTextLayout(D2D1::Point2F(x + 96.0f, y), layout, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        layout->Release();
        y += height + 9.0f;
    };

    auto rowWithHint = [&](const wchar_t* label, const std::wstring& value, const std::wstring& hint) {
        if (value.empty()) {
            return;
        }
        row(label, value);
        if (!hint.empty()) {
            text(fSmall, hint, D2D1::RectF(x + 96.0f, y - 8.0f, x + width, y + 4.0f), kFaint);
            y += 10.0f;
        }
    };

    section(L"TARGET");
    row(L"path", entry.target);
    row(L"arguments", entry.arguments);
    row(L"workdir", entry.workingDir);
    row(L"icon", entry.iconLocation);

    section(L"USAGE");
    rowWithHint(L"last used", formatted(entry.lastModified), relative(entry.lastModified));
    row(L"entry created", formatted(entry.entryCreated));
    if (entry.interactions > 0) {
        row(L"uses", format_int(entry.interactions));
    }
    if (entry.pinned) {
        row(L"pinned", L"yes");
    }
    if (!entry.hostname.empty()) {
        row(L"host", entry.hostname);
    }
    if (!entry.macAddress.empty()) {
        row(L"mac", entry.macAddress);
    }

    section(L"LINK");
    if (entry.fileSize > 0) {
        row(L"size", format_size(entry.fileSize));
    }
    row(L"created", formatted(entry.targetCreated));
    row(L"modified", formatted(entry.targetModified));
    row(L"accessed", formatted(entry.targetAccessed));
    row(L"attributes", entry.attributes);
    row(L"show", entry.showCommand);
    row(L"hotkey", entry.hotkey);
    row(L"drive", entry.driveType);
    row(L"volume", entry.volumeLabel);
    if (!entry.volumeSerial.empty()) {
        row(L"serial", entry.volumeSerial);
    }
    row(L"network", entry.networkName);
    row(L"idlist", entry.idListPath);
    row(L"known folder", entry.knownFolder);
    row(L"environment", entry.environmentTarget);
    row(L"relative", entry.relativePath);
    row(L"blocks", entry.extraBlocks);
    row(L"flags", entry.linkFlags);

    section(L"DESTLIST");
    if (entry.entryNumber != 0) {
        row(L"entry no", format_int(entry.entryNumber));
    }
    row(L"mru", format_int(entry.mruPosition));
    row(L"checksum", entry.checksum);
    row(L"property store", entry.propertyStore);
    row(L"object path", entry.objectPath);
    row(L"resolved", entry.objectPathResolved);

    section(L"TRACKER");
    row(L"machine", entry.machineId);
    row(L"volume id", entry.trackerVolumeId);
    row(L"file id", entry.trackerFileId);
    row(L"volume birth", entry.trackerVolumeBirth);
    row(L"file birth", entry.trackerFileBirth);

    section(L"SOURCE");
    if (source != nullptr) {
        row(L"file", source->path);
        row(L"appid", source->appId);
        row(L"entries", format_int(source->entryCount));
        if (source->destListVersion > 0) {
            row(L"destlist", L"version " + format_int(source->destListVersion));
        }
        if (source->size > 0) {
            row(L"size", format_size(source->size));
        }
        row(L"notes", source->notes);
    }
    row(L"notes", entry.notes);
}

void App::paint_drawer() {
    if (selected < 0) {
        return;
    }
    const Entry* entry = selected_entry();
    if (entry == nullptr) {
        return;
    }
    const D2D1_RECT_F& drawer = layout.drawer;
    fill(drawer, kPanel);
    stroke_line(drawer.left + 0.5f, drawer.top, drawer.left + 0.5f, drawer.bottom, kHair);

    D2D1_RECT_F close = D2D1::RectF(drawer.right - 42.0f, drawer.top + 16.0f, drawer.right - 18.0f, drawer.top + 40.0f);
    if (hoverRow == -4) {
        fill_round(close, 6.0f, kRaised);
    }
    text(fHead, L"x", close, hoverRow == -4 ? kText : kFaint, DWRITE_TEXT_ALIGNMENT_CENTER);

    target->PushAxisAlignedClip(D2D1::RectF(drawer.left, drawer.top, drawer.right, drawer.bottom),
                                D2D1_ANTIALIAS_MODE_ALIASED);
    paint_details(*entry, source_of(*entry), D2D1::RectF(drawer.left, drawer.top, drawer.right - 8.0f, drawer.bottom));
    target->PopAxisAlignedClip();
}

void App::paint_status() {
    const D2D1_RECT_F& status = layout.status;
    fill(status, kPanel);
    stroke_line(status.left, status.top + 0.5f, status.right, status.top + 0.5f, kHair);
    text(fSmall, L"made by diquuuu", D2D1::RectF(24.0f, status.top, 220.0f, status.bottom), kDim);

    std::wstring right;
    if (scanning) {
        right = L"scanning";
    } else if (!dataset.entries.empty()) {
        right = format_int(static_cast<long long>(visible.size())) + L" shown  /  " +
                format_int(static_cast<long long>(dataset.entries.size())) + L" entries";
        if (hiddenFilters > 0) {
            right += L"  /  " + format_int(static_cast<long long>(hiddenFilters)) + L" hidden by filters";
        }
        right += L"  /  " + format_int(static_cast<long long>(dataset.filesScanned)) + L" files visited  /  " +
                 mode_text();
        if (scan.skipped > 0) {
            right += L"  /  " + format_int(scan.skipped) + L" not accessible";
        }
        right += L"  /  " + format_int(static_cast<long long>(std::llround(dataset.seconds))) + L"s";
    } else {
        right = L"ready";
    }
    text(fSmall, right, D2D1::RectF(status.left + 260.0f, status.top, status.right - 24.0f, status.bottom), kFaint,
         DWRITE_TEXT_ALIGNMENT_TRAILING);
}

void App::paint_scan_overlay() {
    if (!scanning) {
        return;
    }
    D2D1_RECT_F area = layout.table;
    fill(area, rgb(0x0B0D0E, 0.86f));

    float total = static_cast<float>(std::max(1, scan.found.load()));
    float progress = static_cast<float>(scan.parsed.load()) / total;
    progress = std::min(1.0f, std::max(0.0f, progress));
    int phase = scan.phase.load();

    float width = std::min(430.0f, area.right - area.left - 80.0f);
    float centerX = (area.left + area.right) * 0.5f;
    float startY = (area.top + area.bottom) * 0.5f - 56.0f;
    std::wstring title = phase == 0 ? L"Looking for jump lists" : L"Reading jump lists";
    text(fBold, title, D2D1::RectF(centerX - width * 0.5f, startY, centerX + width * 0.5f, startY + 24.0f), kText,
         DWRITE_TEXT_ALIGNMENT_CENTER);

    std::wstring detail;
    if (phase == 0) {
        detail = format_int(scan.directories.load()) + L" folders  /  " + format_int(scan.files.load()) +
                 L" files  /  " + format_int(scan.found.load()) + L" found";
        if (scan.skipped > 0) {
            detail += L"  /  " + format_int(scan.skipped) + L" not accessible";
        }
    } else {
        detail = format_int(scan.parsed.load()) + L" of " + format_int(scan.found.load()) + L" files  /  " +
                 format_int(scan.entryCount.load()) + L" entries";
    }
    text(fSmall, detail, D2D1::RectF(centerX - width * 0.5f, startY + 26.0f, centerX + width * 0.5f, startY + 44.0f),
         kDim, DWRITE_TEXT_ALIGNMENT_CENTER);

    D2D1_RECT_F bar = D2D1::RectF(centerX - width * 0.5f, startY + 54.0f, centerX + width * 0.5f, startY + 56.0f);
    fill(bar, rgb(0x22282A));
    if (phase == 0) {
        float t = static_cast<float>(GetTickCount64() % 1400) / 1400.0f;
        float hinge = width * 0.34f;
        float x = bar.left + t * (width - hinge);
        fill(D2D1::RectF(x, bar.top, x + hinge, bar.bottom), kAmber);
    } else {
        fill(D2D1::RectF(bar.left, bar.top, bar.left + width * progress, bar.bottom), kAmber);
    }

    std::wstring current = scan.get_current();
    if (current.size() > 96) {
        current = L"..." + current.substr(current.size() - 93);
    }
    text(fMonoSmall, current,
         D2D1::RectF(centerX - width * 0.5f, startY + 66.0f, centerX + width * 0.5f, startY + 84.0f), kFaint,
         DWRITE_TEXT_ALIGNMENT_CENTER);
}

void App::render() {
    if (!create_devices() || !ensure_target()) {
        return;
    }
    compute_layout();
    target->BeginDraw();
    target->Clear(kBg);
    paint_top();
    paint_summary();
    paint_rail();
    paint_table();
    paint_drawer();
    paint_status();
    paint_scan_overlay();
    HRESULT result = target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        drop_target();
    }
}

void App::on_mouse_move(float x, float y) {
    int previousRow = hoverRow;
    int previousSource = hoverSource;
    int previousBucket = hoverBucket;
    int previousMode = hoverMode;
    int previousFilter = hoverFilter;
    int previousSep = hoverSep;
    mouseX = x;
    mouseY = y;

    if (dragSep >= 0) {
        float tableWidth = layout.table.right - layout.table.left;
        float avail = tableWidth - 96.0f;
        float dx = x - dragStartX;
        if (dragSep == 0) {
            colAppW = std::min(std::max(dragBaseApp + dx, 90.0f),
                               std::max(90.0f, avail - colTargetW - colCountW - 150.0f));
        } else if (dragSep == 1) {
            colTargetW = std::min(std::max(dragBaseTarget + dx, 120.0f),
                                  std::max(120.0f, avail - colAppW - colCountW - 150.0f));
        } else {
            colCountW = std::min(std::max(dragBaseCount + dx, 46.0f),
                                 std::max(46.0f, avail - colAppW - colTargetW - 150.0f));
        }
        compute_layout();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    hoverSep = separator_at(x, y);
    hoverRow = -1;
    hoverSource = -1;
    hoverBucket = -1;
    hoverMode = -1;
    hoverFilter = -1;

    auto inside = [&](const D2D1_RECT_F& rect) {
        return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
    };
    if (inside(layout.rescan)) {
        hoverRow = -2;
    } else if (inside(layout.exportButton)) {
        hoverRow = -3;
    } else if (selected >= 0 && inside(D2D1::RectF(layout.drawer.right - 42.0f, layout.drawer.top + 16.0f,
                                                   layout.drawer.right - 18.0f, layout.drawer.top + 40.0f))) {
        hoverRow = -4;
    } else if (inside(layout.search)) {
        hoverRow = -1;
    }
    if (inside(layout.modeEverywhere)) {
        hoverMode = 0;
    } else if (inside(layout.modeStandard)) {
        hoverMode = 1;
    }
    if (inside(layout.filterBoot)) {
        hoverFilter = 0;
    } else if (inside(layout.filterNoise)) {
        hoverFilter = 1;
    }
    if (y > layout.header.bottom && y < layout.table.bottom && x > layout.table.left && x < layout.table.right) {
        hoverRow = entry_at(y);
    }
    if (y > layout.railList.top && x < layout.rail.right) {
        int index = static_cast<int>((y - layout.railList.top) / 42.0f);
        if (index >= 0) {
            hoverSource = index;
        }
    }
    if (x >= layout.histogram.left && x <= layout.histogram.right && y >= layout.histogram.top &&
        y <= layout.histogram.bottom && !buckets.empty()) {
        float slot = (layout.histogram.right - layout.histogram.left) / static_cast<float>(buckets.size());
        int index = static_cast<int>((x - layout.histogram.left) / slot);
        if (index >= 0 && index < static_cast<int>(buckets.size())) {
            hoverBucket = index;
        }
    }
    if (previousRow != hoverRow || previousSource != hoverSource || previousBucket != hoverBucket ||
        previousMode != hoverMode || previousFilter != hoverFilter || previousSep != hoverSep) {
        InvalidateRect(window, nullptr, FALSE);
    }
}

void App::on_click(float x, float y) {
    auto inside = [&](const D2D1_RECT_F& rect) {
        return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
    };
    int sep = separator_at(x, y);
    if (sep >= 0) {
        dragSep = sep;
        dragStartX = x;
        dragBaseApp = colAppW;
        dragBaseTarget = colTargetW;
        dragBaseCount = colCountW;
        hoverSep = sep;
        SetCapture(window);
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (inside(layout.search)) {
        searchFocused = true;
        caret = static_cast<int>(query.size());
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (inside(layout.rescan)) {
        if (!scanning) {
            start_scan();
        }
        return;
    }
    if (inside(layout.exportButton)) {
        if (!dataset.entries.empty()) {
            export_csv();
        }
        return;
    }
    if (inside(layout.modeEverywhere) || inside(layout.modeStandard)) {
        bool want = inside(layout.modeEverywhere);
        if (want != everywhere && !scanning) {
            everywhere = want;
            start_scan();
        }
        return;
    }
    if (inside(layout.filterBoot)) {
        bootOnly = !bootOnly;
        rebuild();
        selected = -1;
        scroll = 0.0f;
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (inside(layout.filterNoise)) {
        reduceNoise = !reduceNoise;
        rebuild();
        selected = -1;
        scroll = 0.0f;
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (selected >= 0 &&
        inside(D2D1::RectF(layout.drawer.right - 42.0f, layout.drawer.top + 16.0f, layout.drawer.right - 18.0f,
                           layout.drawer.top + 40.0f))) {
        selected = -1;
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (hoverBucket >= 0 && hoverBucket < static_cast<int>(buckets.size())) {
        const std::wstring& day = buckets[static_cast<size_t>(hoverBucket)].day;
        dayFilter = dayFilter == day ? std::wstring() : day;
        rebuild();
        selected = -1;
        scroll = 0.0f;
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (y > layout.header.top && y < layout.header.bottom) {
        if (x >= layout.colApp && x < layout.colTarget) {
            sortColumn = SortColumn::App;
            sortAscending = !sortAscending;
        } else if (x >= layout.colTarget && x < layout.colCount) {
            sortColumn = SortColumn::Target;
            sortAscending = !sortAscending;
        } else if (x >= layout.colCount && x < layout.colModified) {
            sortColumn = SortColumn::Count;
            sortAscending = !sortAscending;
        } else {
            sortColumn = SortColumn::Modified;
            sortAscending = !sortAscending;
        }
        rebuild();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (y > layout.railList.top && x < layout.rail.right) {
        int index = static_cast<int>((y - layout.railList.top) / 42.0f);
        if (index >= 0) {
            int filter = -1;
            if (index == 0) {
                filter = -1;
            } else if (index - 1 < static_cast<int>(railOrder.size())) {
                filter = railOrder[static_cast<size_t>(index - 1)];
            }
            if (filter != sourceFilter) {
                sourceFilter = filter;
                rebuild();
                scroll = 0.0f;
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return;
    }
    int position = entry_at(y);
    if (position >= 0) {
        const Entry& entry = dataset.entries[static_cast<size_t>(visible[static_cast<size_t>(position)])];
        selected = entry.id;
        InvalidateRect(window, nullptr, FALSE);
    } else if (y > layout.header.bottom && y < layout.table.bottom) {
        selected = -1;
        InvalidateRect(window, nullptr, FALSE);
    }
}

void App::on_wheel(float delta) {
    if (mouseX < 0.0f || mouseY < 0.0f) {
        return;
    }
    if (mouseY < layout.header.top || mouseY > layout.table.bottom || mouseX < layout.table.left ||
        mouseX > layout.table.right) {
        return;
    }
    float viewHeight = layout.table.bottom - layout.header.bottom;
    float maxScroll = std::max(0.0f, static_cast<float>(visible.size()) * layout.rowHeight - viewHeight);
    UINT lines = 3;
    if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0)) {
        lines = 3;
    }
    float step;
    if (lines == static_cast<UINT>(0xFFFFFFFF)) {
        step = viewHeight;
    } else {
        step = static_cast<float>(std::max(1u, lines)) * layout.rowHeight;
    }
    scroll -= (delta / 120.0f) * step;
    scroll = std::min(std::max(0.0f, scroll), maxScroll);
    int position = entry_at(mouseY);
    if (hoverRow != position) {
        hoverRow = position;
    }
    InvalidateRect(window, nullptr, FALSE);
}

void App::on_char(wchar_t ch) {
    if (!searchFocused) {
        return;
    }
    if (ch == 8) {
        if (!query.empty() && caret > 0) {
            query.erase(static_cast<size_t>(caret - 1), 1);
            --caret;
        }
    } else if (ch == 27) {
        searchFocused = false;
        query.clear();
        caret = 0;
    } else if (ch >= 32) {
        query.insert(query.begin() + caret, ch);
        ++caret;
    } else {
        return;
    }
    std::wstring needle = lower(query);
    query = needle;
    caret = std::min<int>(caret, static_cast<int>(query.size()));
    rebuild();
    selected = -1;
    scroll = 0.0f;
    InvalidateRect(window, nullptr, FALSE);
}

void App::on_key(int key) {
    if (key == 'F' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        searchFocused = true;
        caret = static_cast<int>(query.size());
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (key == VK_F5 && !scanning) {
        start_scan();
        return;
    }
    if (key == 'E' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        if (!dataset.entries.empty()) {
            export_csv();
        }
        return;
    }
    if (key == VK_ESCAPE) {
        if (searchFocused) {
            searchFocused = false;
            query.clear();
            caret = 0;
            rebuild();
        } else {
            selected = -1;
        }
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (key == VK_DOWN || key == VK_UP) {
        int current = -1;
        for (size_t i = 0; i < visible.size(); ++i) {
            const Entry& entry = dataset.entries[static_cast<size_t>(visible[i])];
            if (entry.id == selected) {
                current = static_cast<int>(i);
                break;
            }
        }
        int next = key == VK_DOWN ? current + 1 : current - 1;
        if (next < 0) {
            next = 0;
        }
        if (next >= static_cast<int>(visible.size())) {
            next = static_cast<int>(visible.size()) - 1;
        }
        if (next >= 0 && next < static_cast<int>(visible.size())) {
            selected = dataset.entries[static_cast<size_t>(visible[static_cast<size_t>(next)])].id;
            ensure_visible(next);
            InvalidateRect(window, nullptr, FALSE);
        }
    }
}

void App::export_csv() {
    wchar_t file[MAX_PATH];
    wcscpy_s(file, default_export_name().c_str());
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"CSV file\0*.csv\0All files\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetSaveFileNameW(&dialog) == 0) {
        return;
    }
    std::wstring error;
    if (!write_csv(file, dataset, error)) {
        MessageBoxW(window, error.c_str(), L"Export failed", MB_ICONERROR | MB_OK);
    }
}

App* g_app = nullptr;

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = g_app;
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (app == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    switch (message) {
        case WM_SIZE: {
            if (app->target != nullptr) {
                RECT client = {};
                GetClientRect(window, &client);
                app->target->Resize(D2D1::SizeU(std::max<LONG>(1, client.right),
                                                std::max<LONG>(1, client.bottom)));
            }
            app->compute_layout();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_DPICHANGED: {
            app->dpi = HIWORD(wParam);
            app->scale = static_cast<float>(app->dpi) / 96.0f;
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            app->drop_target();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint = {};
            BeginPaint(window, &paint);
            app->render();
            EndPaint(window, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE:
            app->on_mouse_move(static_cast<float>(GET_X_LPARAM(lParam)) / app->scale,
                               static_cast<float>(GET_Y_LPARAM(lParam)) / app->scale);
            return 0;
        case WM_LBUTTONDOWN:
            SetFocus(window);
            app->on_click(static_cast<float>(GET_X_LPARAM(lParam)) / app->scale,
                          static_cast<float>(GET_Y_LPARAM(lParam)) / app->scale);
            return 0;
        case WM_LBUTTONUP:
            if (app->dragSep >= 0) {
                app->dragSep = -1;
                ReleaseCapture();
                app->compute_layout();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_SETCURSOR:
            if (app->dragSep >= 0 || app->hoverSep >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_MOUSEWHEEL:
            app->on_wheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)));
            return 0;
        case WM_CHAR:
            app->on_char(static_cast<wchar_t>(wParam));
            return 0;
        case WM_KEYDOWN:
            app->on_key(static_cast<int>(wParam));
            return 0;
        case WM_TIMER: {
            if (app->done.load()) {
                app->finish_scan();
            } else if (app->scanning) {
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = static_cast<LONG>(1040 * app->scale);
            info->ptMinTrackSize.y = static_cast<LONG>(640 * app->scale);
            return 0;
        }
        case WM_CLOSE:
            app->closing = true;
            app->scan.stop = true;
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

}

int run(HINSTANCE instance, int showCommand, bool everywhere) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto app = std::make_unique<App>();
    app->instance = instance;
    app->everywhere = everywhere;
    g_app = app.get();

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = window_proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = L"jumplens.window";
    if (RegisterClassExW(&windowClass) == 0) {
        return 1;
    }

    app->dpi = GetDpiForSystem();
    app->scale = static_cast<float>(app->dpi) / 96.0f;
    int width = static_cast<int>(1440 * app->scale);
    int height = static_cast<int>(880 * app->scale);
    RECT desired = {0, 0, width, height};
    AdjustWindowRectEx(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0);

    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"jumplens", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                  CW_USEDEFAULT, desired.right - desired.left, desired.bottom - desired.top,
                                  nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        return 1;
    }
    app->window = window;
    app->dpi = GetDpiForWindow(window);
    app->scale = static_cast<float>(app->dpi) / 96.0f;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(window, 19, &dark, sizeof(dark));

    ShowWindow(window, showCommand == 0 ? SW_SHOWNORMAL : showCommand);
    UpdateWindow(window);

    SetTimer(window, 1, 60, nullptr);
    app->start_scan();

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (app->worker.joinable()) {
        app->scan.stop = true;
        app->worker.join();
    }
    app->release();
    g_app = nullptr;
    CoUninitialize();
    return 0;
}

}
}
