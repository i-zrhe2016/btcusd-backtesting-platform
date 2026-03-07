#include "data_feed.h"
#include "embedded_btcusd_data.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

namespace {

constexpr UINT kDefaultDpi = 96;
constexpr UINT_PTR kPlaybackTimerId = 1;
constexpr int kControlHeight = 28;
constexpr int kTopBarHeight = 42;
constexpr int kAxisLabelHeight = 18;
constexpr int kBottomBarHeight = 28;
constexpr int kButtonWidth = 78;
constexpr int kSmallButtonWidth = 42;
constexpr int kSliderWidth = 260;
constexpr int kTrendButtonWidth = 92;
constexpr double kChartShiftRatio = 0.20;
constexpr int kMinChartShiftSlots = 4;

enum ControlId : int {
    kOpenButton = 1001,
    kPlayPauseButton,
    kPrevButton,
    kNextButton,
    kSpeedButton,
    kTrendlineButton,
    kTimeframe1HButton,
    kTimeframe4HButton,
    kTimeframeDButton,
    kTimeframeWButton,
    kProgressSlider,
};

struct TrendPoint {
    std::int64_t timestamp = 0;
    double price = 0.0;
};

struct Trendline {
    TrendPoint start;
    TrendPoint end;
};

struct ChartView {
    RECT rect{};
    const std::vector<Candle>* candles = nullptr;
    int start = 0;
    int end = 0;
    int visible_count = 0;
    int data_visible_count = 0;
    int right_padding_slots = 0;
    double step = 0.0;
    double min_price = 0.0;
    double max_price = 0.0;
    bool valid = false;
};

struct AppState {
    HWND window = nullptr;
    HWND open_button = nullptr;
    HWND play_pause_button = nullptr;
    HWND prev_button = nullptr;
    HWND next_button = nullptr;
    HWND speed_button = nullptr;
    HWND trendline_button = nullptr;
    HWND tf_1h_button = nullptr;
    HWND tf_4h_button = nullptr;
    HWND tf_d_button = nullptr;
    HWND tf_w_button = nullptr;
    HWND progress_slider = nullptr;
    HFONT ui_font = nullptr;
    HFONT small_font = nullptr;

    std::wstring current_file = L"Built-in demo data";
    std::vector<Candle> base_candles;
    std::vector<Candle> candles_1h;
    std::vector<Candle> candles_4h;
    std::vector<Candle> candles_d1;
    std::vector<Candle> candles_w1;

    Timeframe timeframe = Timeframe::H1;
    std::array<double, 4> speed_options{1.0, 2.0, 4.0, 8.0};
    int speed_index = 0;
    bool playing = false;
    bool dragging_slider = false;
    std::size_t playback_index = 0;
    std::int64_t playback_timestamp = 0;
    double playback_accumulator = 0.0;
    ULONGLONG last_tick_ms = 0;
    UINT dpi = kDefaultDpi;
    std::vector<Trendline> trendlines;
    bool trendline_mode = false;
    bool trendline_draft_active = false;
    TrendPoint trendline_draft_start;
    TrendPoint trendline_draft_current;
};

AppState g_app;

RECT ChartRect();

std::wstring AsciiToWide(const char* text) {
    if (text == nullptr) {
        return {};
    }
    const std::size_t length = std::strlen(text);
    return std::wstring(text, text + length);
}

std::wstring BuildEmbeddedDataLabel() {
    std::wstring label = L"Built-in BTC-USD 1H (";
    label += AsciiToWide(embedded_btcusd_data::kExchange);
    label += L", ";
    label += AsciiToWide(embedded_btcusd_data::kCoverageStartUtc);
    label += L" -> ";
    label += AsciiToWide(embedded_btcusd_data::kCoverageEndUtc);
    label += L")";
    return label;
}

bool PointInRectStrict(const RECT& rect, const POINT& point) {
    return point.x >= rect.left && point.x < rect.right &&
           point.y >= rect.top && point.y < rect.bottom;
}

POINT ClampPointToRect(const RECT& rect, POINT point) {
    point.x = std::clamp(point.x, rect.left, rect.right - 1);
    point.y = std::clamp(point.y, rect.top, rect.bottom - 1);
    return point;
}

int ScaleByDpi(int value) {
    return MulDiv(value, static_cast<int>(g_app.dpi), static_cast<int>(kDefaultDpi));
}

using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
using SetProcessDpiAwareFn = BOOL(WINAPI*)();
using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
using GetDpiForSystemFn = UINT(WINAPI*)();
using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);

UINT GetSystemDpi() {
    if (const HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        const auto get_dpi_for_system =
            reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"));
        if (get_dpi_for_system != nullptr) {
            return get_dpi_for_system();
        }
    }

    HDC screen_dc = GetDC(nullptr);
    const int dpi = screen_dc != nullptr ? GetDeviceCaps(screen_dc, LOGPIXELSX) : static_cast<int>(kDefaultDpi);
    if (screen_dc != nullptr) {
        ReleaseDC(nullptr, screen_dc);
    }
    return dpi > 0 ? static_cast<UINT>(dpi) : kDefaultDpi;
}

UINT GetWindowDpi(HWND window) {
    if (window != nullptr) {
        if (const HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            const auto get_dpi_for_window =
                reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
            if (get_dpi_for_window != nullptr) {
                return get_dpi_for_window(window);
            }
        }
    }

    return GetSystemDpi();
}

void EnableHighDpi() {
    if (const HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        const auto set_dpi_awareness_context =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_dpi_awareness_context != nullptr &&
            set_dpi_awareness_context(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))) != FALSE) {
            return;
        }

        const auto set_process_dpi_aware =
            reinterpret_cast<SetProcessDpiAwareFn>(GetProcAddress(user32, "SetProcessDPIAware"));
        if (set_process_dpi_aware != nullptr) {
            set_process_dpi_aware();
        }
    }
}

HFONT CreateScaledFont(int point_size) {
    const int height = -MulDiv(point_size, static_cast<int>(g_app.dpi), 72);
    return CreateFontW(
        height,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );
}

void ApplyFontToControls(HFONT font) {
    if (font == nullptr) {
        return;
    }

    for (HWND control : {g_app.open_button, g_app.play_pause_button, g_app.prev_button, g_app.next_button,
                         g_app.speed_button, g_app.trendline_button, g_app.tf_1h_button, g_app.tf_4h_button,
                         g_app.tf_d_button, g_app.tf_w_button, g_app.progress_slider}) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }
}

void RecreateFonts() {
    if (g_app.ui_font != nullptr) {
        DeleteObject(g_app.ui_font);
        g_app.ui_font = nullptr;
    }
    if (g_app.small_font != nullptr) {
        DeleteObject(g_app.small_font);
        g_app.small_font = nullptr;
    }

    g_app.ui_font = CreateScaledFont(10);
    g_app.small_font = CreateScaledFont(9);
    ApplyFontToControls(g_app.ui_font);
}

RECT ScaleWindowRectForDpi(UINT dpi, int client_width, int client_height, DWORD style, DWORD ex_style) {
    RECT rect{
        0,
        0,
        MulDiv(client_width, static_cast<int>(dpi), static_cast<int>(kDefaultDpi)),
        MulDiv(client_height, static_cast<int>(dpi), static_cast<int>(kDefaultDpi))
    };

    if (const HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        const auto adjust_window_rect_for_dpi =
            reinterpret_cast<AdjustWindowRectExForDpiFn>(
                GetProcAddress(user32, "AdjustWindowRectExForDpi"));
        if (adjust_window_rect_for_dpi != nullptr) {
            adjust_window_rect_for_dpi(&rect, style, FALSE, ex_style, dpi);
            return rect;
        }
    }

    AdjustWindowRectEx(&rect, style, FALSE, ex_style);
    return rect;
}

const std::vector<Candle>& CurrentCandles() {
    switch (g_app.timeframe) {
        case Timeframe::H1:
            return g_app.candles_1h;
        case Timeframe::H4:
            return g_app.candles_4h;
        case Timeframe::D1:
            return g_app.candles_d1;
        case Timeframe::W1:
            return g_app.candles_w1;
    }

    return g_app.candles_1h;
}

std::size_t PlaybackIndexForTimestamp(const std::vector<Candle>& candles, std::int64_t timestamp) {
    if (candles.empty()) {
        return 0;
    }

    const auto it = std::upper_bound(
        candles.begin(), candles.end(), timestamp,
        [](std::int64_t value, const Candle& candle) {
            return value < candle.timestamp;
        });
    if (it == candles.begin()) {
        return 0;
    }
    return static_cast<std::size_t>((it - candles.begin()) - 1);
}

ChartView BuildCurrentChartView() {
    ChartView view;
    view.rect = ChartRect();

    const std::vector<Candle>& candles = CurrentCandles();
    if (candles.empty()) {
        return view;
    }

    view.candles = &candles;
    const int width = view.rect.right - view.rect.left;
    view.visible_count = std::max(20, width / std::max(1, ScaleByDpi(11)));
    const int max_padding_slots = std::max(1, view.visible_count - 1);
    const int min_padding_slots = std::min(kMinChartShiftSlots, max_padding_slots);
    // Reserve a small "future" area on the right, similar to MT4 chart shift.
    view.right_padding_slots = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(view.visible_count) * kChartShiftRatio)),
        min_padding_slots,
        max_padding_slots);
    view.data_visible_count = std::max(1, view.visible_count - view.right_padding_slots);
    view.end = static_cast<int>(g_app.playback_index);
    view.start = std::max(0, view.end - view.data_visible_count + 1);
    if (view.end < view.start) {
        return view;
    }

    view.min_price = candles[view.start].low;
    view.max_price = candles[view.start].high;
    for (int i = view.start; i <= view.end; ++i) {
        view.min_price = std::min(view.min_price, candles[i].low);
        view.max_price = std::max(view.max_price, candles[i].high);
    }

    const double padding = (view.max_price - view.min_price) * 0.08;
    view.min_price -= padding;
    view.max_price += padding;
    view.step = static_cast<double>(width) / static_cast<double>(std::max(view.visible_count, 1));
    view.valid = true;
    return view;
}

void ClampPlaybackTimestamp() {
    if (g_app.base_candles.empty()) {
        g_app.playback_timestamp = 0;
        return;
    }
    g_app.playback_timestamp = std::clamp(
        g_app.playback_timestamp,
        g_app.base_candles.front().timestamp,
        g_app.base_candles.back().timestamp);
}

void SyncPlaybackIndexToTimestamp() {
    const std::vector<Candle>& candles = CurrentCandles();
    if (candles.empty()) {
        g_app.playback_index = 0;
        return;
    }
    g_app.playback_index = PlaybackIndexForTimestamp(candles, g_app.playback_timestamp);
}

void UpdateButtonText(HWND button, const std::wstring& text) {
    SetWindowTextW(button, text.c_str());
}

void UpdateWindowTitle() {
    std::wstring title = L"BTCUSD Replay - " + g_app.current_file;
    SetWindowTextW(g_app.window, title.c_str());
}

void UpdateTrackbarRange() {
    const std::vector<Candle>& candles = CurrentCandles();
    const int max_value = candles.empty() ? 0 : static_cast<int>(candles.size() - 1);
    SendMessageW(g_app.progress_slider, TBM_SETRANGEMIN, TRUE, 0);
    SendMessageW(g_app.progress_slider, TBM_SETRANGEMAX, TRUE, max_value);
    SendMessageW(g_app.progress_slider, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(g_app.progress_slider, TBM_SETPOS, TRUE, static_cast<LPARAM>(g_app.playback_index));
}

void UpdatePlayPauseButton() {
    UpdateButtonText(g_app.play_pause_button, g_app.playing ? L"Pause" : L"Play");
}

void UpdateSpeedButton() {
    std::wostringstream stream;
    stream << L"Speed x" << static_cast<int>(g_app.speed_options[g_app.speed_index]);
    UpdateButtonText(g_app.speed_button, stream.str());
}

void UpdateTrendlineButton() {
    UpdateButtonText(g_app.trendline_button, g_app.trendline_mode ? L"[Trend]" : L"Trend");
}

void UpdateTimeframeButtons() {
    UpdateButtonText(g_app.tf_1h_button, g_app.timeframe == Timeframe::H1 ? L"[1H]" : L"1H");
    UpdateButtonText(g_app.tf_4h_button, g_app.timeframe == Timeframe::H4 ? L"[4H]" : L"4H");
    UpdateButtonText(g_app.tf_d_button, g_app.timeframe == Timeframe::D1 ? L"[D]" : L"D");
    UpdateButtonText(g_app.tf_w_button, g_app.timeframe == Timeframe::W1 ? L"[W]" : L"W");
}

void RefreshUiState() {
    ClampPlaybackTimestamp();
    SyncPlaybackIndexToTimestamp();
    UpdatePlayPauseButton();
    UpdateSpeedButton();
    UpdateTrendlineButton();
    UpdateTimeframeButtons();
    UpdateTrackbarRange();
    UpdateWindowTitle();
    InvalidateRect(g_app.window, nullptr, FALSE);
}

void RebuildAggregates() {
    g_app.candles_1h = AggregateCandles(g_app.base_candles, Timeframe::H1);
    g_app.candles_4h = AggregateCandles(g_app.base_candles, Timeframe::H4);
    g_app.candles_d1 = AggregateCandles(g_app.base_candles, Timeframe::D1);
    g_app.candles_w1 = AggregateCandles(g_app.base_candles, Timeframe::W1);
}

void StopPlayback() {
    g_app.playing = false;
    g_app.playback_accumulator = 0.0;
    g_app.last_tick_ms = GetTickCount64();
}

void SetTimeframe(Timeframe timeframe) {
    g_app.timeframe = timeframe;
    RefreshUiState();
}

void StepPlayback(int delta) {
    const std::vector<Candle>& candles = CurrentCandles();
    if (candles.empty()) {
        return;
    }

    const int current = static_cast<int>(PlaybackIndexForTimestamp(candles, g_app.playback_timestamp));
    const int last = static_cast<int>(candles.size() - 1);
    const int next = std::clamp(current + delta, 0, last);
    g_app.playback_timestamp = candles[static_cast<std::size_t>(next)].timestamp;
    g_app.playback_index = static_cast<std::size_t>(next);
    RefreshUiState();
}

void TogglePlayback() {
    const std::vector<Candle>& candles = CurrentCandles();
    if (candles.empty()) {
        return;
    }

    const std::size_t current_index = PlaybackIndexForTimestamp(candles, g_app.playback_timestamp);
    if (current_index >= candles.size() - 1) {
        g_app.playback_timestamp = candles.front().timestamp;
        g_app.playback_index = 0;
    }

    g_app.playing = !g_app.playing;
    g_app.playback_accumulator = 0.0;
    g_app.last_tick_ms = GetTickCount64();
    RefreshUiState();
}

void CycleSpeed() {
    g_app.speed_index = (g_app.speed_index + 1) % static_cast<int>(g_app.speed_options.size());
    RefreshUiState();
}

void LoadBaseCandles(std::vector<Candle> candles, const std::wstring& name) {
    g_app.base_candles = std::move(candles);
    RebuildAggregates();
    g_app.timeframe = Timeframe::H1;
    g_app.playback_index = 0;
    g_app.playback_timestamp = g_app.base_candles.empty() ? 0 : g_app.base_candles.front().timestamp;
    g_app.playback_accumulator = 0.0;
    g_app.playing = false;
    g_app.current_file = name;
    g_app.trendlines.clear();
    g_app.trendline_mode = false;
    g_app.trendline_draft_active = false;
    RefreshUiState();
}

void ShowLoadError(const std::string& error) {
    const std::wstring message(error.begin(), error.end());
    MessageBoxW(g_app.window, message.c_str(), L"Load Error", MB_ICONERROR | MB_OK);
}

void LoadDefaultDataset() {
    std::string error;
    std::vector<Candle> candles = LoadCandlesFromCsvText(embedded_btcusd_data::kCsv, &error);
    if (!candles.empty()) {
        LoadBaseCandles(std::move(candles), BuildEmbeddedDataLabel());
        return;
    }

    LoadBaseCandles(GenerateDemoCandles(), L"Built-in demo data");
    if (!error.empty()) {
        ShowLoadError(error);
    }
}

void OpenCsvDialog() {
    wchar_t file_path[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_app.window;
    dialog.lpstrFilter = L"CSV Files\0*.csv\0All Files\0*.*\0";
    dialog.lpstrFile = file_path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&dialog)) {
        return;
    }

    std::string error;
    std::vector<Candle> candles = LoadCandlesFromCsv(file_path, &error);
    if (candles.empty()) {
        ShowLoadError(error);
        return;
    }

    LoadBaseCandles(std::move(candles), file_path);
}

void LayoutControls() {
    RECT client{};
    GetClientRect(g_app.window, &client);

    int x = ScaleByDpi(8);
    const int y = ScaleByDpi(7);
    const int control_height = ScaleByDpi(kControlHeight);
    const int button_width = ScaleByDpi(kButtonWidth);
    const int small_button_width = ScaleByDpi(kSmallButtonWidth);
    const int slider_width = ScaleByDpi(kSliderWidth);
    const int speed_width = ScaleByDpi(96);
    const int trend_width = ScaleByDpi(kTrendButtonWidth);
    const int gap = ScaleByDpi(6);

    MoveWindow(g_app.open_button, x, y, button_width, control_height, TRUE);
    x += button_width + gap;
    MoveWindow(g_app.play_pause_button, x, y, button_width, control_height, TRUE);
    x += button_width + gap;
    MoveWindow(g_app.prev_button, x, y, small_button_width, control_height, TRUE);
    x += small_button_width + gap;
    MoveWindow(g_app.next_button, x, y, small_button_width, control_height, TRUE);
    x += small_button_width + gap;
    MoveWindow(g_app.speed_button, x, y, speed_width, control_height, TRUE);
    x += speed_width + gap;
    MoveWindow(g_app.trendline_button, x, y, trend_width, control_height, TRUE);

    x = client.right - (slider_width + 4 * (small_button_width + gap) + ScaleByDpi(8));
    x = std::max(x, ScaleByDpi(520));
    MoveWindow(g_app.tf_1h_button, x, y, small_button_width, control_height, TRUE);
    x += small_button_width + gap;
    MoveWindow(g_app.tf_4h_button, x, y, small_button_width, control_height, TRUE);
    x += small_button_width + gap;
    MoveWindow(g_app.tf_d_button, x, y, small_button_width, control_height, TRUE);
    x += small_button_width + gap;
    MoveWindow(g_app.tf_w_button, x, y, small_button_width, control_height, TRUE);
    x += small_button_width + gap;
    MoveWindow(g_app.progress_slider, x, y, slider_width, control_height, TRUE);
}

void CreateControls(HWND window) {
    g_app.open_button = CreateWindowExW(0, L"BUTTON", L"Open CSV",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kOpenButton), nullptr, nullptr);
    g_app.play_pause_button = CreateWindowExW(0, L"BUTTON", L"Play",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kPlayPauseButton), nullptr, nullptr);
    g_app.prev_button = CreateWindowExW(0, L"BUTTON", L"<",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kPrevButton), nullptr, nullptr);
    g_app.next_button = CreateWindowExW(0, L"BUTTON", L">",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kNextButton), nullptr, nullptr);
    g_app.speed_button = CreateWindowExW(0, L"BUTTON", L"Speed x1",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kSpeedButton), nullptr, nullptr);
    g_app.trendline_button = CreateWindowExW(0, L"BUTTON", L"Trend",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTrendlineButton), nullptr, nullptr);
    g_app.tf_1h_button = CreateWindowExW(0, L"BUTTON", L"[1H]",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframe1HButton), nullptr, nullptr);
    g_app.tf_4h_button = CreateWindowExW(0, L"BUTTON", L"4H",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframe4HButton), nullptr, nullptr);
    g_app.tf_d_button = CreateWindowExW(0, L"BUTTON", L"D",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframeDButton), nullptr, nullptr);
    g_app.tf_w_button = CreateWindowExW(0, L"BUTTON", L"W",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframeWButton), nullptr, nullptr);
    g_app.progress_slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kProgressSlider), nullptr, nullptr);

    RecreateFonts();
    LayoutControls();
}

RECT ChartRect() {
    RECT client{};
    GetClientRect(g_app.window, &client);
    client.top += ScaleByDpi(kTopBarHeight);
    client.bottom -= ScaleByDpi(kBottomBarHeight + kAxisLabelHeight);
    return client;
}

std::wstring BuildStatusLine() {
    const std::vector<Candle>& candles = CurrentCandles();
    if (candles.empty()) {
        return L"No data";
    }

    const Candle& candle = candles[g_app.playback_index];
    std::wostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(2);
    stream << TimeframeLabel(g_app.timeframe)
           << L"  "
           << FormatTimestamp(candle.timestamp)
           << L"  O:" << candle.open
           << L" H:" << candle.high
           << L" L:" << candle.low
           << L" C:" << candle.close
           << L"  Speed:x" << static_cast<int>(g_app.speed_options[g_app.speed_index])
           << L"  Lines:" << g_app.trendlines.size()
           << L"  Trend:" << (g_app.trendline_mode ? L"ON" : L"OFF")
           << L"  "
           << (g_app.playing ? L"Playing" : L"Paused");
    if (g_app.trendline_draft_active) {
        stream << L"  Trendline: click second point";
    } else if (g_app.trendline_mode) {
        stream << L"  LClick: trendline  Del: undo";
    } else {
        stream << L"  Trend/T: draw line  Del: undo";
    }
    return stream.str();
}

void DrawTextInRect(HDC dc, const RECT& rect, const std::wstring& text, COLORREF color, HFONT font, UINT flags) {
    HGDIOBJ old_font = nullptr;
    if (font != nullptr) {
        old_font = SelectObject(dc, font);
    }
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, const_cast<RECT*>(&rect), flags);
    if (old_font != nullptr) {
        SelectObject(dc, old_font);
    }
}

int PriceToY(double price, double min_price, double max_price, const RECT& chart_rect) {
    if (max_price - min_price < 0.0001) {
        return (chart_rect.top + chart_rect.bottom) / 2;
    }

    const double ratio = (max_price - price) / (max_price - min_price);
    return chart_rect.top + static_cast<int>(ratio * static_cast<double>(chart_rect.bottom - chart_rect.top));
}

double YToPrice(int y, double min_price, double max_price, const RECT& chart_rect) {
    const int height = std::max(1, static_cast<int>(chart_rect.bottom - chart_rect.top));
    const double ratio = static_cast<double>(y - chart_rect.top) / static_cast<double>(height);
    return max_price - ratio * (max_price - min_price);
}

double TimestampToIndexPosition(const std::vector<Candle>& candles, std::int64_t timestamp) {
    if (candles.empty()) {
        return 0.0;
    }
    if (candles.size() == 1) {
        return 0.0;
    }

    const auto compare = [](const Candle& candle, std::int64_t value) {
        return candle.timestamp < value;
    };
    const auto it = std::lower_bound(candles.begin(), candles.end(), timestamp, compare);
    if (it == candles.begin()) {
        const double span = static_cast<double>(std::max<std::int64_t>(1, candles[1].timestamp - candles[0].timestamp));
        return static_cast<double>(timestamp - candles[0].timestamp) / span;
    }
    if (it == candles.end()) {
        const std::size_t last = candles.size() - 1;
        const double span = static_cast<double>(std::max<std::int64_t>(1, candles[last].timestamp - candles[last - 1].timestamp));
        return static_cast<double>(last) + static_cast<double>(timestamp - candles[last].timestamp) / span;
    }
    if (it->timestamp == timestamp) {
        return static_cast<double>(it - candles.begin());
    }

    const std::size_t next_index = static_cast<std::size_t>(it - candles.begin());
    const std::size_t prev_index = next_index - 1;
    const double span = static_cast<double>(std::max<std::int64_t>(1, candles[next_index].timestamp - candles[prev_index].timestamp));
    return static_cast<double>(prev_index) +
           static_cast<double>(timestamp - candles[prev_index].timestamp) / span;
}

std::int64_t IndexPositionToTimestamp(const std::vector<Candle>& candles, double position) {
    if (candles.empty()) {
        return 0;
    }
    if (candles.size() == 1) {
        return candles.front().timestamp;
    }

    if (position <= 0.0) {
        const double span = static_cast<double>(std::max<std::int64_t>(1, candles[1].timestamp - candles[0].timestamp));
        return static_cast<std::int64_t>(std::llround(static_cast<double>(candles[0].timestamp) + position * span));
    }

    const double last_index = static_cast<double>(candles.size() - 1);
    if (position >= last_index) {
        const std::size_t last = candles.size() - 1;
        const double span = static_cast<double>(std::max<std::int64_t>(1, candles[last].timestamp - candles[last - 1].timestamp));
        return static_cast<std::int64_t>(std::llround(static_cast<double>(candles[last].timestamp) + (position - last_index) * span));
    }

    const std::size_t left_index = static_cast<std::size_t>(std::floor(position));
    const std::size_t right_index = std::min(left_index + 1, candles.size() - 1);
    const double fraction = position - static_cast<double>(left_index);
    const double left_timestamp = static_cast<double>(candles[left_index].timestamp);
    const double span = static_cast<double>(candles[right_index].timestamp - candles[left_index].timestamp);
    return static_cast<std::int64_t>(std::llround(left_timestamp + fraction * span));
}

int TimestampToX(const ChartView& view, std::int64_t timestamp) {
    if (!view.valid || view.candles == nullptr) {
        return view.rect.left;
    }
    const double position = TimestampToIndexPosition(*view.candles, timestamp);
    const double x = static_cast<double>(view.rect.left) +
                     ((position - static_cast<double>(view.start)) + 0.5) * view.step;
    return static_cast<int>(std::lround(x));
}

TrendPoint PointToTrendPoint(const ChartView& view, POINT point) {
    const POINT clamped = ClampPointToRect(view.rect, point);
    const double relative = (static_cast<double>(clamped.x - view.rect.left) / view.step) - 0.5;
    const double position = static_cast<double>(view.start) + relative;

    TrendPoint anchor;
    anchor.timestamp = IndexPositionToTimestamp(*view.candles, position);
    anchor.price = YToPrice(clamped.y, view.min_price, view.max_price, view.rect);
    return anchor;
}

void CancelTrendlineDraft() {
    if (!g_app.trendline_draft_active) {
        return;
    }

    g_app.trendline_draft_active = false;
    ReleaseCapture();
    InvalidateRect(g_app.window, nullptr, FALSE);
}

void UpdateTrendlineDraft(const TrendPoint& point) {
    if (!g_app.trendline_draft_active) {
        return;
    }

    g_app.trendline_draft_current = point;
    InvalidateRect(g_app.window, nullptr, FALSE);
}

void StartTrendlineDraft(const TrendPoint& point) {
    g_app.trendline_draft_active = true;
    g_app.trendline_draft_start = point;
    g_app.trendline_draft_current = point;
    SetCapture(g_app.window);
    InvalidateRect(g_app.window, nullptr, FALSE);
}

void CommitTrendlineDraft(const TrendPoint& point) {
    if (!g_app.trendline_draft_active) {
        return;
    }

    g_app.trendlines.push_back(Trendline{g_app.trendline_draft_start, point});
    g_app.trendline_draft_active = false;
    ReleaseCapture();
    InvalidateRect(g_app.window, nullptr, FALSE);
}

void DrawTrendline(HDC dc, const ChartView& view, const Trendline& trendline, COLORREF color, int width) {
    if (!view.valid) {
        return;
    }

    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, TimestampToX(view, trendline.start.timestamp),
        PriceToY(trendline.start.price, view.min_price, view.max_price, view.rect), nullptr);
    LineTo(dc, TimestampToX(view, trendline.end.timestamp),
        PriceToY(trendline.end.price, view.min_price, view.max_price, view.rect));
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void DrawCandles(HDC dc, const RECT& chart_rect) {
    const ChartView view = BuildCurrentChartView();
    if (!view.valid || view.candles == nullptr) {
        DrawTextInRect(dc, chart_rect, L"Open a CSV file or use the built-in BTCUSD demo data.",
            RGB(180, 180, 180), g_app.ui_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    const std::vector<Candle>& candles = *view.candles;
    const int shown = view.end - view.start + 1;
    if (shown <= 0) {
        return;
    }

    SaveDC(dc);
    IntersectClipRect(dc, view.rect.left, view.rect.top, view.rect.right, view.rect.bottom);

    const int candle_width = std::max(ScaleByDpi(3), static_cast<int>(view.step * 0.65));

    for (int i = view.start; i <= view.end; ++i) {
        const Candle& candle = candles[i];
        const double center = chart_rect.left + (static_cast<double>(i - view.start) + 0.5) * view.step;
        const int x = static_cast<int>(center);

        const int high_y = PriceToY(candle.high, view.min_price, view.max_price, chart_rect);
        const int low_y = PriceToY(candle.low, view.min_price, view.max_price, chart_rect);
        const int open_y = PriceToY(candle.open, view.min_price, view.max_price, chart_rect);
        const int close_y = PriceToY(candle.close, view.min_price, view.max_price, chart_rect);
        const bool bull = candle.close >= candle.open;
        const COLORREF wick_color = RGB(255, 255, 255);
        const COLORREF outline_color = bull ? RGB(255, 255, 255) : RGB(210, 210, 210);
        const COLORREF fill_color = bull ? RGB(0, 0, 0) : RGB(255, 255, 255);

        HPEN wick_pen = CreatePen(PS_SOLID, 1, wick_color);
        HGDIOBJ old_pen = SelectObject(dc, wick_pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

        MoveToEx(dc, x, high_y, nullptr);
        LineTo(dc, x, low_y);

        HPEN body_pen = CreatePen(PS_SOLID, 1, outline_color);
        HBRUSH brush = CreateSolidBrush(fill_color);
        SelectObject(dc, body_pen);
        SelectObject(dc, brush);

        RECT body{
            x - candle_width / 2,
            std::min(open_y, close_y),
            x + candle_width / 2,
            std::max(open_y, close_y)
        };
        if (body.bottom - body.top < 2) {
            body.bottom = body.top + 2;
        }
        Rectangle(dc, body.left, body.top, body.right, body.bottom);

        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(wick_pen);
        DeleteObject(body_pen);
        DeleteObject(brush);

        if ((i - view.start) % std::max(6, view.data_visible_count / 6) == 0 || i == view.end) {
            RECT label_rect{
                x - ScaleByDpi(48),
                chart_rect.bottom + ScaleByDpi(2),
                x + ScaleByDpi(48),
                chart_rect.bottom + ScaleByDpi(18)
            };
            DrawTextInRect(dc, label_rect, FormatTimestamp(candle.timestamp), RGB(155, 155, 160),
                g_app.small_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    for (const Trendline& trendline : g_app.trendlines) {
        DrawTrendline(dc, view, trendline, RGB(210, 210, 210), std::max(1, ScaleByDpi(2)));
    }
    if (g_app.trendline_draft_active) {
        DrawTrendline(dc, view,
            Trendline{g_app.trendline_draft_start, g_app.trendline_draft_current},
            RGB(120, 120, 120), std::max(1, ScaleByDpi(1)));
    }

    RestoreDC(dc, -1);
}

void DrawScene(HDC window_dc) {
    RECT client{};
    GetClientRect(g_app.window, &client);
    if (client.right <= 0 || client.bottom <= 0) {
        return;
    }

    HDC memory_dc = CreateCompatibleDC(window_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(window_dc, client.right, client.bottom);
    HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);

    HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memory_dc, &client, background);
    DeleteObject(background);

    RECT chart_rect = ChartRect();
    HBRUSH chart_bg = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memory_dc, &chart_rect, chart_bg);
    DeleteObject(chart_bg);

    DrawCandles(memory_dc, chart_rect);

    RECT status_rect{
        ScaleByDpi(8),
        client.bottom - ScaleByDpi(kBottomBarHeight) + ScaleByDpi(2),
        client.right - ScaleByDpi(8),
        client.bottom - ScaleByDpi(4)
    };
    DrawTextInRect(memory_dc, status_rect, BuildStatusLine(), RGB(220, 220, 220), g_app.ui_font,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(window_dc, 0, 0, client.right, client.bottom, memory_dc, 0, 0, SRCCOPY);

    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
}

void HandlePlaybackTick() {
    if (!g_app.playing) {
        return;
    }

    const std::vector<Candle>& candles = CurrentCandles();
    if (candles.empty()) {
        StopPlayback();
        RefreshUiState();
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const double elapsed = static_cast<double>(now - g_app.last_tick_ms) / 1000.0;
    g_app.last_tick_ms = now;
    g_app.playback_accumulator += elapsed * g_app.speed_options[g_app.speed_index];

    const int advance = static_cast<int>(std::floor(g_app.playback_accumulator));
    if (advance <= 0) {
        return;
    }

    g_app.playback_accumulator -= advance;
    const std::size_t current_index = PlaybackIndexForTimestamp(candles, g_app.playback_timestamp);
    const std::size_t next_index = std::min(current_index + static_cast<std::size_t>(advance), candles.size() - 1);
    g_app.playback_timestamp = candles[next_index].timestamp;
    g_app.playback_index = next_index;
    if (g_app.playback_index >= candles.size() - 1) {
        StopPlayback();
    }

    if (!g_app.dragging_slider) {
        SendMessageW(g_app.progress_slider, TBM_SETPOS, TRUE, static_cast<LPARAM>(g_app.playback_index));
    }
    InvalidateRect(g_app.window, nullptr, FALSE);
    UpdatePlayPauseButton();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
        case WM_CREATE:
            g_app.window = window;
            g_app.dpi = GetWindowDpi(window);
            CreateControls(window);
            SetTimer(window, kPlaybackTimerId, 33, nullptr);
            LoadDefaultDataset();
            return 0;

        case WM_DPICHANGED: {
            g_app.dpi = HIWORD(w_param);
            RecreateFonts();
            const RECT* suggested = reinterpret_cast<const RECT*>(l_param);
            if (suggested != nullptr) {
                SetWindowPos(window, nullptr,
                    suggested->left,
                    suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            LayoutControls();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        }

        case WM_SIZE:
            LayoutControls();
            InvalidateRect(window, nullptr, FALSE);
            return 0;

        case WM_COMMAND: {
            const int command = LOWORD(w_param);
            switch (command) {
                case kOpenButton:
                    OpenCsvDialog();
                    return 0;
                case kPlayPauseButton:
                    TogglePlayback();
                    return 0;
                case kPrevButton:
                    StopPlayback();
                    StepPlayback(-1);
                    return 0;
                case kNextButton:
                    StopPlayback();
                    StepPlayback(1);
                    return 0;
                case kSpeedButton:
                    CycleSpeed();
                    return 0;
                case kTrendlineButton:
                    g_app.trendline_mode = !g_app.trendline_mode;
                    if (!g_app.trendline_mode) {
                        CancelTrendlineDraft();
                    }
                    RefreshUiState();
                    return 0;
                case kTimeframe1HButton:
                    StopPlayback();
                    SetTimeframe(Timeframe::H1);
                    return 0;
                case kTimeframe4HButton:
                    StopPlayback();
                    SetTimeframe(Timeframe::H4);
                    return 0;
                case kTimeframeDButton:
                    StopPlayback();
                    SetTimeframe(Timeframe::D1);
                    return 0;
                case kTimeframeWButton:
                    StopPlayback();
                    SetTimeframe(Timeframe::W1);
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(l_param) == g_app.progress_slider) {
                const UINT code = LOWORD(w_param);
                if (code == TB_THUMBTRACK || code == SB_THUMBTRACK) {
                    g_app.dragging_slider = true;
                }

                const std::size_t next_index = static_cast<std::size_t>(
                    SendMessageW(g_app.progress_slider, TBM_GETPOS, 0, 0));
                const std::vector<Candle>& candles = CurrentCandles();
                g_app.playback_index = next_index;
                if (!candles.empty()) {
                    g_app.playback_timestamp = candles[std::min(next_index, candles.size() - 1)].timestamp;
                }
                InvalidateRect(window, nullptr, FALSE);

                if (code == TB_ENDTRACK || code == SB_ENDSCROLL || code == TB_THUMBPOSITION) {
                    g_app.dragging_slider = false;
                }
                return 0;
            }
            break;

        case WM_LBUTTONDOWN: {
            SetFocus(window);
            const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            const ChartView view = BuildCurrentChartView();
            if (g_app.trendline_mode && view.valid && PointInRectStrict(view.rect, point)) {
                const TrendPoint anchor = PointToTrendPoint(view, point);
                if (g_app.trendline_draft_active) {
                    CommitTrendlineDraft(anchor);
                } else {
                    StartTrendlineDraft(anchor);
                }
                return 0;
            }
            break;
        }

        case WM_MOUSEMOVE:
            if (g_app.trendline_draft_active) {
                const ChartView view = BuildCurrentChartView();
                if (view.valid) {
                    POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
                    point = ClampPointToRect(view.rect, point);
                    UpdateTrendlineDraft(PointToTrendPoint(view, point));
                }
                return 0;
            }
            break;

        case WM_RBUTTONDOWN:
            if (g_app.trendline_draft_active) {
                CancelTrendlineDraft();
                return 0;
            }
            break;

        case WM_TIMER:
            if (w_param == kPlaybackTimerId) {
                HandlePlaybackTick();
                return 0;
            }
            break;

        case WM_KEYDOWN:
            switch (w_param) {
                case VK_SPACE:
                    TogglePlayback();
                    return 0;
                case VK_LEFT:
                    StopPlayback();
                    StepPlayback(-1);
                    return 0;
                case VK_RIGHT:
                    StopPlayback();
                    StepPlayback(1);
                    return 0;
                case '1':
                    StopPlayback();
                    SetTimeframe(Timeframe::H1);
                    return 0;
                case '4':
                    StopPlayback();
                    SetTimeframe(Timeframe::H4);
                    return 0;
                case 'D':
                    StopPlayback();
                    SetTimeframe(Timeframe::D1);
                    return 0;
                case 'W':
                    StopPlayback();
                    SetTimeframe(Timeframe::W1);
                    return 0;
                case 'S':
                    CycleSpeed();
                    return 0;
                case 'T':
                    g_app.trendline_mode = !g_app.trendline_mode;
                    if (!g_app.trendline_mode) {
                        CancelTrendlineDraft();
                    }
                    RefreshUiState();
                    return 0;
                case VK_ESCAPE:
                    CancelTrendlineDraft();
                    return 0;
                case VK_DELETE:
                case VK_BACK:
                    if (g_app.trendline_draft_active) {
                        CancelTrendlineDraft();
                    } else if (!g_app.trendlines.empty()) {
                        g_app.trendlines.pop_back();
                        InvalidateRect(window, nullptr, FALSE);
                    }
                    return 0;
                default:
                    break;
            }
            break;

        case WM_CAPTURECHANGED:
            if (g_app.trendline_draft_active && reinterpret_cast<HWND>(l_param) != window) {
                g_app.trendline_draft_active = false;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            DrawScene(dc);
            EndPaint(window, &paint);
            return 0;
        }

        case WM_DESTROY:
            if (g_app.ui_font != nullptr) {
                DeleteObject(g_app.ui_font);
                g_app.ui_font = nullptr;
            }
            if (g_app.small_font != nullptr) {
                DeleteObject(g_app.small_font);
                g_app.small_font = nullptr;
            }
            KillTimer(window, kPlaybackTimerId);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    EnableHighDpi();
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    const wchar_t* class_name = L"BtcUsdReplayWindow";
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = class_name;

    RegisterClassExW(&window_class);

    constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
    constexpr DWORD kWindowExStyle = 0;
    g_app.dpi = GetSystemDpi();
    const RECT window_rect = ScaleWindowRectForDpi(g_app.dpi, 1360, 820, kWindowStyle, kWindowExStyle);

    HWND window = CreateWindowExW(
        kWindowExStyle,
        class_name,
        L"BTCUSD Replay",
        kWindowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (window == nullptr) {
        return 0;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr && argc >= 2) {
        std::string error;
        std::vector<Candle> candles = LoadCandlesFromCsv(argv[1], &error);
        if (!candles.empty()) {
            LoadBaseCandles(std::move(candles), argv[1]);
        } else if (!error.empty()) {
            ShowLoadError(error);
        }
    }
    if (argv != nullptr) {
        LocalFree(argv);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
