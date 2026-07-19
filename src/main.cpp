#include "data_feed.h"
#include "embedded_btcusd_data.h"
#include "stoch_indicator.h"

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
#include <cwctype>
#include <initializer_list>
#include <memory>
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
constexpr int kTopBarHeight = 74;
constexpr int kAxisLabelHeight = 18;
constexpr int kBottomBarHeight = 28;
constexpr int kStochPanelHeight = 190;
constexpr int kStochPanelGap = 8;
constexpr int kMinStochPanelHeight = 110;
constexpr int kMaxStochPanelHeight = 420;
constexpr int kStochResizeHitHeight = 10;
constexpr int kButtonWidth = 72;
constexpr int kSmallButtonWidth = 36;
constexpr int kTimeframeButtonWidth = 42;
constexpr int kSliderWidth = 260;
constexpr int kTrendButtonWidth = 70;
constexpr int kSettingsButtonWidth = 86;
constexpr int kHotkeysButtonWidth = 66;
constexpr double kChartShiftRatio = 0.20;
constexpr int kMinChartShiftSlots = 4;

enum ControlId : int {
    kOpenButton = 1001,
    kNewWindowButton,
    kStatusButton,
    kPlayPauseButton,
    kPrevButton,
    kNextButton,
    kSpeedButton,
    kTrendlineButton,
    kStochButton,
    kStochParametersButton,
    kHotkeysButton,
    kTimeframe1mButton,
    kTimeframe15mButton,
    kTimeframe30mButton,
    kTimeframe1HButton,
    kTimeframe2HButton,
    kTimeframe4HButton,
    kTimeframeDButton,
    kTimeframeWButton,
    kTimeframeMButton,
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

struct TradeMarker {
    std::int64_t timestamp = 0;
    double price = 0.0;
    bool buy = true;
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

enum class ShortcutAction : std::size_t {
    PlayPause,
    Previous,
    Next,
    Speed,
    Trendline,
    StochVisibility,
    Buy,
    Sell,
    M1,
    M15,
    M30,
    H1,
    H2,
    H4,
    D1,
    W1,
    MN1,
};

constexpr std::size_t kShortcutCount = 17;

struct ShortcutBindings {
    std::array<UINT, kShortcutCount> keys{
        VK_SPACE, VK_LEFT, VK_RIGHT, 'S', 'T', 'O', 'B', 'V', '6',
        '5', '3', '1', '2', '4', 'D', 'W', 'M'
    };
};

struct AppState {
    HWND window = nullptr;
    HWND open_button = nullptr;
    HWND new_window_button = nullptr;
    HWND status_button = nullptr;
    HWND play_pause_button = nullptr;
    HWND prev_button = nullptr;
    HWND next_button = nullptr;
    HWND speed_button = nullptr;
    HWND trendline_button = nullptr;
    HWND stoch_button = nullptr;
    HWND stoch_parameters_button = nullptr;
    HWND hotkeys_button = nullptr;
    HWND tf_1m_button = nullptr;
    HWND tf_15m_button = nullptr;
    HWND tf_30m_button = nullptr;
    HWND tf_1h_button = nullptr;
    HWND tf_2h_button = nullptr;
    HWND tf_4h_button = nullptr;
    HWND tf_d_button = nullptr;
    HWND tf_w_button = nullptr;
    HWND tf_m_button = nullptr;
    HWND progress_slider = nullptr;
    HFONT ui_font = nullptr;
    HFONT small_font = nullptr;

    std::wstring current_file = L"Built-in demo data";
    std::vector<Candle> base_candles;
    std::vector<Candle> candles_1m;
    std::vector<Candle> candles_15m;
    std::vector<Candle> candles_30m;
    std::vector<Candle> candles_1h;
    std::vector<Candle> candles_2h;
    std::vector<Candle> candles_4h;
    std::vector<Candle> candles_d1;
    std::vector<Candle> candles_w1;
    std::vector<Candle> candles_mn1;

    Timeframe timeframe = Timeframe::M1;
    std::array<double, 4> speed_options{1.0, 2.0, 4.0, 8.0};
    int speed_index = 0;
    bool playing = false;
    bool dragging_slider = false;
    bool chart_drag_active = false;
    POINT chart_drag_start{};
    std::size_t chart_drag_start_end = 0;
    std::size_t chart_end_index = 0;
    bool chart_follow_playback = true;
    std::size_t playback_index = 0;
    std::int64_t playback_timestamp = 0;
    double playback_accumulator = 0.0;
    ULONGLONG last_tick_ms = 0;
    UINT dpi = kDefaultDpi;
    std::vector<Trendline> trendlines;
    std::vector<TradeMarker> trade_markers;
    bool trendline_mode = false;
    bool trendline_draft_active = false;
    TrendPoint trendline_draft_start;
    TrendPoint trendline_draft_current;
    bool stoch_visible = true;
    int stoch_panel_height = kStochPanelHeight;
    bool stoch_resize_active = false;
    bool status_visible = true;
    int chart_zoom = 1;
    std::array<StochParameters, 9> stoch_parameters{};
    ShortcutBindings shortcuts;
    StochSeries stoch_series;
};

AppState g_app;

RECT ChartRect();
void InvalidateChartAndStatus();
void CancelTrendlineDraft();

std::wstring AsciiToWide(const char* text) {
    if (text == nullptr) {
        return {};
    }
    const std::size_t length = std::strlen(text);
    return std::wstring(text, text + length);
}

std::wstring BuildEmbeddedDataLabel() {
    std::wstring label = L"Built-in BTC-USD ";
    label += AsciiToWide(embedded_btcusd_data::kGranularity);
    label += L" (";
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

    for (HWND control : {g_app.open_button, g_app.new_window_button, g_app.status_button,
                         g_app.play_pause_button, g_app.prev_button, g_app.next_button,
                         g_app.speed_button, g_app.trendline_button, g_app.stoch_button,
                         g_app.stoch_parameters_button, g_app.hotkeys_button,
                         g_app.tf_1m_button, g_app.tf_15m_button, g_app.tf_30m_button, g_app.tf_1h_button,
                         g_app.tf_2h_button, g_app.tf_4h_button, g_app.tf_d_button,
                         g_app.tf_w_button, g_app.tf_m_button, g_app.progress_slider}) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }
}

std::size_t TimeframeIndex(Timeframe timeframe) {
    switch (timeframe) {
        case Timeframe::M1: return 0;
        case Timeframe::M15: return 1;
        case Timeframe::M30: return 2;
        case Timeframe::H1: return 3;
        case Timeframe::H2: return 4;
        case Timeframe::H4: return 5;
        case Timeframe::D1: return 6;
        case Timeframe::W1: return 7;
        case Timeframe::MN1: return 8;
    }
    return 3;
}

StochParameters& CurrentStochParameters() {
    return g_app.stoch_parameters[TimeframeIndex(g_app.timeframe)];
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
        case Timeframe::M1:
            return g_app.candles_1m;
        case Timeframe::M15:
            return g_app.candles_15m;
        case Timeframe::M30:
            return g_app.candles_30m;
        case Timeframe::H1:
            return g_app.candles_1h;
        case Timeframe::H2:
            return g_app.candles_2h;
        case Timeframe::H4:
            return g_app.candles_4h;
        case Timeframe::D1:
            return g_app.candles_d1;
        case Timeframe::W1:
            return g_app.candles_w1;
        case Timeframe::MN1:
            return g_app.candles_mn1;
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
    const int candle_slot_width = std::max(1, ScaleByDpi(11 * g_app.chart_zoom));
    view.visible_count = std::max(20, width / candle_slot_width);
    const int max_padding_slots = std::max(1, view.visible_count - 1);
    const int min_padding_slots = std::min(kMinChartShiftSlots, max_padding_slots);
    // Reserve a small "future" area on the right, similar to MT4 chart shift.
    view.right_padding_slots = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(view.visible_count) * kChartShiftRatio)),
        min_padding_slots,
        max_padding_slots);
    view.data_visible_count = std::max(1, view.visible_count - view.right_padding_slots);
    const std::size_t requested_end = g_app.chart_follow_playback
        ? g_app.playback_index
        : g_app.chart_end_index;
    view.end = static_cast<int>(std::min(requested_end, candles.size() - 1));
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

void UpdateStochButton() {
    UpdateButtonText(g_app.stoch_button, g_app.stoch_visible ? L"[Stoch 3]" : L"Stoch Off");
}

void UpdateSettingsButtons() {
    UpdateButtonText(g_app.stoch_parameters_button, L"Stoch Params");
    UpdateButtonText(g_app.hotkeys_button, L"Hotkeys");
}

void UpdateStatusButton() {
    UpdateButtonText(g_app.status_button, g_app.status_visible ? L"Status On" : L"Status Off");
}

void UpdateTimeframeButtons() {
    UpdateButtonText(g_app.tf_1m_button, g_app.timeframe == Timeframe::M1 ? L"[1m]" : L"1m");
    UpdateButtonText(g_app.tf_15m_button, g_app.timeframe == Timeframe::M15 ? L"[15m]" : L"15m");
    UpdateButtonText(g_app.tf_30m_button, g_app.timeframe == Timeframe::M30 ? L"[30m]" : L"30m");
    UpdateButtonText(g_app.tf_1h_button, g_app.timeframe == Timeframe::H1 ? L"[1h]" : L"1h");
    UpdateButtonText(g_app.tf_2h_button, g_app.timeframe == Timeframe::H2 ? L"[2h]" : L"2h");
    UpdateButtonText(g_app.tf_4h_button, g_app.timeframe == Timeframe::H4 ? L"[4h]" : L"4h");
    UpdateButtonText(g_app.tf_d_button, g_app.timeframe == Timeframe::D1 ? L"[D]" : L"D");
    UpdateButtonText(g_app.tf_w_button, g_app.timeframe == Timeframe::W1 ? L"[W]" : L"W");
    UpdateButtonText(g_app.tf_m_button, g_app.timeframe == Timeframe::MN1 ? L"[M]" : L"M");
}

void RefreshUiState() {
    ClampPlaybackTimestamp();
    SyncPlaybackIndexToTimestamp();
    if (g_app.chart_follow_playback) {
        g_app.chart_end_index = g_app.playback_index;
    }
    UpdatePlayPauseButton();
    UpdateSpeedButton();
    UpdateTrendlineButton();
    UpdateStochButton();
    UpdateSettingsButtons();
    UpdateStatusButton();
    UpdateTimeframeButtons();
    UpdateTrackbarRange();
    UpdateWindowTitle();
    InvalidateRect(g_app.window, nullptr, FALSE);
}

void RebuildAggregates() {
    std::int64_t smallest_interval = 0;
    for (std::size_t i = 1; i < g_app.base_candles.size(); ++i) {
        const std::int64_t interval = g_app.base_candles[i].timestamp -
            g_app.base_candles[i - 1].timestamp;
        if (interval > 0 && (smallest_interval == 0 || interval < smallest_interval)) {
            smallest_interval = interval;
        }
    }

    // A higher-timeframe candle cannot be safely split into lower-timeframe
    // candles. Keep the buttons available, but leave unsupported lower
    // periods empty until the user loads suitably granular CSV data.
    g_app.candles_1m = AggregateCandles(g_app.base_candles, Timeframe::M1);
    g_app.candles_15m = smallest_interval == 0 || smallest_interval <= 15 * 60
        ? AggregateCandles(g_app.base_candles, Timeframe::M15) : std::vector<Candle>{};
    g_app.candles_30m = smallest_interval == 0 || smallest_interval <= 30 * 60
        ? AggregateCandles(g_app.base_candles, Timeframe::M30) : std::vector<Candle>{};
    g_app.candles_1h = AggregateCandles(g_app.base_candles, Timeframe::H1);
    g_app.candles_2h = AggregateCandles(g_app.base_candles, Timeframe::H2);
    g_app.candles_4h = AggregateCandles(g_app.base_candles, Timeframe::H4);
    g_app.candles_d1 = AggregateCandles(g_app.base_candles, Timeframe::D1);
    g_app.candles_w1 = AggregateCandles(g_app.base_candles, Timeframe::W1);
    g_app.candles_mn1 = AggregateCandles(g_app.base_candles, Timeframe::MN1);
}

void RebuildStochSeries() {
    g_app.stoch_series = CalculateStochSeries(CurrentCandles(), CurrentStochParameters());
}

void InitializeStochParameters() {
    for (std::size_t index = 0; index < g_app.stoch_parameters.size(); ++index) {
        const Timeframe timeframe = index == 0 ? Timeframe::M1
            : index == 1 ? Timeframe::M15
            : index == 2 ? Timeframe::M30
            : index == 3 ? Timeframe::H1
            : index == 4 ? Timeframe::H2
            : index == 5 ? Timeframe::H4
            : index == 6 ? Timeframe::D1
            : index == 7 ? Timeframe::W1
            : Timeframe::MN1;
        g_app.stoch_parameters[index] = DefaultStochParameters(timeframe);
    }
}

void StopPlayback() {
    g_app.playing = false;
    g_app.playback_accumulator = 0.0;
    g_app.last_tick_ms = GetTickCount64();
    if (g_app.play_pause_button != nullptr) {
        UpdatePlayPauseButton();
    }
}

void SetTimeframe(Timeframe timeframe) {
    g_app.timeframe = timeframe;
    g_app.chart_follow_playback = true;
    RebuildStochSeries();
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
    g_app.chart_follow_playback = true;
    g_app.chart_end_index = g_app.playback_index;
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

    g_app.chart_follow_playback = true;
    g_app.chart_end_index = g_app.playback_index;
    g_app.playing = !g_app.playing;
    g_app.playback_accumulator = 0.0;
    g_app.last_tick_ms = GetTickCount64();
    RefreshUiState();
}

void CycleSpeed() {
    g_app.speed_index = (g_app.speed_index + 1) % static_cast<int>(g_app.speed_options.size());
    RefreshUiState();
}

void CycleStochVisibility() {
    g_app.stoch_visible = !g_app.stoch_visible;
    RefreshUiState();
}

void PlaceTradeMarker(bool buy) {
    const std::vector<Candle>& candles = CurrentCandles();
    if (candles.empty() || g_app.playback_index >= candles.size()) {
        return;
    }

    const Candle& candle = candles[g_app.playback_index];
    g_app.trade_markers.push_back(TradeMarker{candle.timestamp, candle.close, buy});
    RefreshUiState();
}

void AdjustChartZoom(int wheel_delta) {
    if (wheel_delta == 0) {
        return;
    }
    const int direction = wheel_delta > 0 ? 1 : -1;
    const int next_zoom = std::clamp(g_app.chart_zoom + direction, 1, 8);
    if (next_zoom == g_app.chart_zoom) {
        return;
    }
    g_app.chart_zoom = next_zoom;
    InvalidateChartAndStatus();
}

void LoadBaseCandles(std::vector<Candle> candles, const std::wstring& name) {
    g_app.base_candles = std::move(candles);
    RebuildAggregates();
    g_app.timeframe = Timeframe::M1;
    g_app.playback_index = 0;
    g_app.chart_end_index = 0;
    g_app.chart_follow_playback = true;
    g_app.playback_timestamp = g_app.base_candles.empty() ? 0 : g_app.base_candles.front().timestamp;
    g_app.playback_accumulator = 0.0;
    g_app.playing = false;
    g_app.current_file = name;
    RebuildStochSeries();
    g_app.trendlines.clear();
    g_app.trade_markers.clear();
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

void OpenNewWindow() {
    wchar_t module_path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        MessageBoxW(g_app.window, L"无法定位当前程序，不能创建新窗口。", L"New Window", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring parameters;
    if (!g_app.current_file.empty() &&
        GetFileAttributesW(g_app.current_file.c_str()) != INVALID_FILE_ATTRIBUTES) {
        parameters = L"\"" + g_app.current_file + L"\"";
    }

    const HINSTANCE result = ShellExecuteW(
        g_app.window, L"open", module_path,
        parameters.empty() ? nullptr : parameters.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(g_app.window, L"创建新窗口失败。", L"New Window", MB_OK | MB_ICONWARNING);
    }
}

constexpr int kSettingsOk = 1;
constexpr int kSettingsCancel = 2;
constexpr int kSettingsFirstEdit = 100;

struct SettingsDialogContext {
    HWND window = nullptr;
    HWND owner = nullptr;
    bool shortcuts = false;
    std::array<HWND, 5> stoch_edits{};
    std::array<HWND, kShortcutCount> shortcut_edits{};
};

const std::array<const wchar_t*, kShortcutCount>& ShortcutLabels() {
    static const std::array<const wchar_t*, kShortcutCount> labels{
        L"Play / Pause", L"Previous candle", L"Next candle", L"Change speed",
        L"Trendline mode", L"Show / hide Stoch", L"Buy marker", L"Sell marker",
        L"1m timeframe", L"15m timeframe", L"30m timeframe",
        L"1h timeframe", L"2h timeframe", L"4h timeframe", L"D timeframe",
        L"W timeframe", L"M timeframe"
    };
    return labels;
}

std::wstring TrimWide(std::wstring value) {
    const auto is_space = [](wchar_t character) {
        return std::iswspace(static_cast<wint_t>(character)) != 0;
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](wchar_t ch) {
        return !is_space(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](wchar_t ch) {
        return !is_space(ch);
    }).base(), value.end());
    return value;
}

std::wstring ShortcutText(UINT key) {
    switch (key) {
        case VK_SPACE: return L"Space";
        case VK_LEFT: return L"Left";
        case VK_RIGHT: return L"Right";
        case VK_UP: return L"Up";
        case VK_DOWN: return L"Down";
        case VK_DELETE: return L"Delete";
        case VK_ESCAPE: return L"Esc";
        case VK_RETURN: return L"Enter";
        case VK_TAB: return L"Tab";
        default:
            if (key >= 'A' && key <= 'Z') {
                return std::wstring(1, static_cast<wchar_t>(key));
            }
            if (key >= '0' && key <= '9') {
                return std::wstring(1, static_cast<wchar_t>(key));
            }
            return L"Unknown";
    }
}

bool ParseShortcutText(const std::wstring& source, UINT* key) {
    std::wstring value = TrimWide(source);
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towupper(static_cast<wint_t>(character)));
    });
    if (value.size() == 1 &&
        ((value[0] >= L'A' && value[0] <= L'Z') || (value[0] >= L'0' && value[0] <= L'9'))) {
        *key = static_cast<UINT>(value[0]);
        return true;
    }
    if (value == L"SPACE") { *key = VK_SPACE; return true; }
    if (value == L"LEFT") { *key = VK_LEFT; return true; }
    if (value == L"RIGHT") { *key = VK_RIGHT; return true; }
    if (value == L"UP") { *key = VK_UP; return true; }
    if (value == L"DOWN") { *key = VK_DOWN; return true; }
    if (value == L"DELETE" || value == L"DEL") { *key = VK_DELETE; return true; }
    if (value == L"ESC" || value == L"ESCAPE") { *key = VK_ESCAPE; return true; }
    if (value == L"ENTER" || value == L"RETURN") { *key = VK_RETURN; return true; }
    if (value == L"TAB") { *key = VK_TAB; return true; }
    return false;
}

void SetControlFont(HWND control) {
    if (control != nullptr && g_app.ui_font != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.ui_font), TRUE);
    }
}

HWND CreateSettingsStatic(HWND parent, const wchar_t* text, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, width, height, parent, nullptr, nullptr, nullptr);
    SetControlFont(control);
    return control;
}

HWND CreateSettingsEdit(HWND parent, const wchar_t* text, int id, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr, nullptr);
    SetControlFont(control);
    return control;
}

void CreateSettingsDialogControls(SettingsDialogContext* context) {
    if (context->shortcuts) {
        CreateSettingsStatic(context->window,
            L"输入单个字母/数字，或 Space、Left、Right、Up、Down、Delete、Esc、Enter、Tab",
            18, 14, 520, 28);
        const auto& labels = ShortcutLabels();
        for (std::size_t index = 0; index < kShortcutCount; ++index) {
            const int y = 47 + static_cast<int>(index) * 27;
            CreateSettingsStatic(context->window, labels[index], 18, y + 3, 220, 21);
            context->shortcut_edits[index] = CreateSettingsEdit(
                context->window, ShortcutText(g_app.shortcuts.keys[index]).c_str(),
                kSettingsFirstEdit + static_cast<int>(index), 250, y, 90, 23);
        }
        const int shortcut_note_y = 47 + static_cast<int>(kShortcutCount) * 27 + 6;
        CreateSettingsStatic(context->window, L"修改后点击应用；按键冲突时以设置表中靠后的动作为准。",
            18, shortcut_note_y, 520, 22);
    } else {
        std::wstring title = L"当前周期：" + TimeframeLabel(g_app.timeframe) +
            L"（每组参数为 length，K/D 平滑长度按 Pine 规则自动计算）";
        CreateSettingsStatic(context->window, title.c_str(), 18, 16, 520, 30);
        for (std::size_t index = 0; index < 5; ++index) {
            const int y = 58 + static_cast<int>(index) * 34;
            std::wstring label = L"Group " + std::to_wstring(index + 1) + L" length";
            CreateSettingsStatic(context->window, label.c_str(), 30, y + 3, 180, 23);
            context->stoch_edits[index] = CreateSettingsEdit(
                context->window, std::to_wstring(CurrentStochParameters().lengths[index]).c_str(),
                kSettingsFirstEdit + static_cast<int>(index), 220, y, 100, 25);
        }
        CreateSettingsStatic(context->window, L"范围：1 - 100000；参数按周期保存，切换周期不会丢失。",
            30, 238, 420, 24);
    }

    HWND ok = CreateWindowExW(0, L"BUTTON", L"Apply",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        context->shortcuts ? 350 : 250, context->shortcuts ? 550 : 282, 86, 28,
        context->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsOk)), nullptr, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        context->shortcuts ? 446 : 346, context->shortcuts ? 550 : 282, 86, 28,
        context->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsCancel)), nullptr, nullptr);
    SetControlFont(ok);
    SetControlFont(cancel);
}

bool ReadEditText(HWND edit, std::wstring* value) {
    const int length = GetWindowTextLengthW(edit);
    if (length <= 0) {
        return false;
    }
    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(edit, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(length));
    *value = std::move(result);
    return true;
}

bool ApplySettingsDialog(SettingsDialogContext* context) {
    if (!context->shortcuts) {
        StochParameters next = CurrentStochParameters();
        for (std::size_t index = 0; index < next.lengths.size(); ++index) {
            std::wstring text;
            if (!ReadEditText(context->stoch_edits[index], &text)) {
                MessageBoxW(context->window, L"每个指标参数都必须填写正整数。", L"Invalid parameter", MB_OK | MB_ICONWARNING);
                return false;
            }
            wchar_t* end = nullptr;
            const long value = std::wcstol(text.c_str(), &end, 10);
            if (end == text.c_str() || *end != L'\0' || value < 1 || value > 100000) {
                MessageBoxW(context->window, L"指标参数必须是 1 到 100000 之间的整数。", L"Invalid parameter", MB_OK | MB_ICONWARNING);
                return false;
            }
            next.lengths[index] = static_cast<int>(value);
        }
        CurrentStochParameters() = next;
        RebuildStochSeries();
        RefreshUiState();
        return true;
    }

    ShortcutBindings next = g_app.shortcuts;
    for (std::size_t index = 0; index < kShortcutCount; ++index) {
        std::wstring text;
        UINT key = 0;
        if (!ReadEditText(context->shortcut_edits[index], &text) || !ParseShortcutText(text, &key)) {
            MessageBoxW(context->window,
                L"快捷键格式无效。请输入一个字母/数字，或 Space、Left、Right 等名称。",
                L"Invalid shortcut", MB_OK | MB_ICONWARNING);
            return false;
        }
        next.keys[index] = key;
    }
    g_app.shortcuts = next;
    RefreshUiState();
    return true;
}

LRESULT CALLBACK SettingsDialogProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* context = reinterpret_cast<SettingsDialogContext*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        context = static_cast<SettingsDialogContext*>(create->lpCreateParams);
        context->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }

    switch (message) {
        case WM_CREATE:
            CreateSettingsDialogControls(context);
            return 0;
        case WM_COMMAND:
            if (LOWORD(w_param) == kSettingsOk) {
                if (ApplySettingsDialog(context)) {
                    DestroyWindow(window);
                }
                return 0;
            }
            if (LOWORD(w_param) == kSettingsCancel) {
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void ShowSettingsDialog(bool shortcuts) {
    static const wchar_t* class_name = L"BtcUsdReplaySettingsDialog";
    static bool class_registered = false;
    if (!class_registered) {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = SettingsDialogProc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        window_class.lpszClassName = class_name;
        if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        class_registered = true;
    }

    SettingsDialogContext context;
    context.owner = g_app.window;
    context.shortcuts = shortcuts;
    const int width = ScaleByDpi(570);
    const int height = ScaleByDpi(shortcuts ? 610 : 350);
    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        class_name,
        shortcuts ? L"Keyboard shortcuts" : L"Stoch indicator parameters",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        g_app.window, nullptr, GetModuleHandleW(nullptr), &context);
    if (dialog == nullptr) {
        return;
    }

    RECT owner_rect{};
    GetWindowRect(g_app.window, &owner_rect);
    SetWindowPos(dialog, HWND_TOP,
        owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2,
        owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2,
        0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    EnableWindow(g_app.window, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetFocus(GetDlgItem(dialog, kSettingsFirstEdit));

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(g_app.window, TRUE);
    SetForegroundWindow(g_app.window);
}

bool HandleConfiguredShortcut(UINT key) {
    // Scan from the end so that, if the user deliberately assigns the same
    // key twice, the later row in the settings dialog wins.
    for (std::size_t reverse = kShortcutCount; reverse > 0; --reverse) {
        if (g_app.shortcuts.keys[reverse - 1] != key) {
            continue;
        }
        switch (static_cast<ShortcutAction>(reverse - 1)) {
            case ShortcutAction::PlayPause:
                TogglePlayback();
                return true;
            case ShortcutAction::Previous:
                StopPlayback();
                StepPlayback(-1);
                return true;
            case ShortcutAction::Next:
                StopPlayback();
                StepPlayback(1);
                return true;
            case ShortcutAction::Speed:
                CycleSpeed();
                return true;
            case ShortcutAction::Trendline:
                g_app.trendline_mode = !g_app.trendline_mode;
                if (!g_app.trendline_mode) {
                    CancelTrendlineDraft();
                }
                RefreshUiState();
                return true;
            case ShortcutAction::StochVisibility:
                CycleStochVisibility();
                return true;
            case ShortcutAction::Buy:
                PlaceTradeMarker(true);
                return true;
            case ShortcutAction::Sell:
                PlaceTradeMarker(false);
                return true;
            case ShortcutAction::M1:
                StopPlayback(); SetTimeframe(Timeframe::M1); return true;
            case ShortcutAction::M15:
                StopPlayback(); SetTimeframe(Timeframe::M15); return true;
            case ShortcutAction::M30:
                StopPlayback(); SetTimeframe(Timeframe::M30); return true;
            case ShortcutAction::H1:
                StopPlayback(); SetTimeframe(Timeframe::H1); return true;
            case ShortcutAction::H2:
                StopPlayback(); SetTimeframe(Timeframe::H2); return true;
            case ShortcutAction::H4:
                StopPlayback(); SetTimeframe(Timeframe::H4); return true;
            case ShortcutAction::D1:
                StopPlayback(); SetTimeframe(Timeframe::D1); return true;
            case ShortcutAction::W1:
                StopPlayback(); SetTimeframe(Timeframe::W1); return true;
            case ShortcutAction::MN1:
                StopPlayback(); SetTimeframe(Timeframe::MN1); return true;
        }
    }
    return false;
}

void LayoutControls() {
    RECT client{};
    GetClientRect(g_app.window, &client);

    int x = ScaleByDpi(8);
    const int y = ScaleByDpi(7);
    const int control_height = ScaleByDpi(kControlHeight);
    const int button_width = ScaleByDpi(kButtonWidth);
    const int small_button_width = ScaleByDpi(kSmallButtonWidth);
    const int timeframe_button_width = ScaleByDpi(kTimeframeButtonWidth);
    const int slider_width = ScaleByDpi(kSliderWidth);
    const int speed_width = ScaleByDpi(80);
    const int trend_width = ScaleByDpi(kTrendButtonWidth);
    const int stoch_width = ScaleByDpi(70);
    const int params_width = ScaleByDpi(kSettingsButtonWidth);
    const int hotkeys_width = ScaleByDpi(kHotkeysButtonWidth);
    const int gap = ScaleByDpi(6);
    const int secondary_y = y + control_height + gap;

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
    x += trend_width + gap;
    MoveWindow(g_app.stoch_button, x, y, stoch_width, control_height, TRUE);
    x += stoch_width + gap;
    MoveWindow(g_app.stoch_parameters_button, x, y, params_width, control_height, TRUE);
    x += params_width + gap;
    MoveWindow(g_app.hotkeys_button, x, y, hotkeys_width, control_height, TRUE);
    const int left_controls_end = x + hotkeys_width;

    x = client.right - (slider_width + 9 * (timeframe_button_width + gap) + ScaleByDpi(8));
    x = std::max(x, left_controls_end + gap);
    for (HWND button : {g_app.tf_1m_button, g_app.tf_15m_button, g_app.tf_30m_button, g_app.tf_1h_button,
                        g_app.tf_2h_button, g_app.tf_4h_button, g_app.tf_d_button,
                        g_app.tf_w_button, g_app.tf_m_button}) {
        MoveWindow(button, x, y, timeframe_button_width, control_height, TRUE);
        x += timeframe_button_width + gap;
    }
    MoveWindow(g_app.progress_slider, x, y, slider_width, control_height, TRUE);

    MoveWindow(g_app.new_window_button, ScaleByDpi(8), secondary_y,
        ScaleByDpi(92), control_height, TRUE);
    MoveWindow(g_app.status_button, ScaleByDpi(8) + ScaleByDpi(92) + gap, secondary_y,
        ScaleByDpi(82), control_height, TRUE);
}

void CreateControls(HWND window) {
    g_app.open_button = CreateWindowExW(0, L"BUTTON", L"Open CSV",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kOpenButton), nullptr, nullptr);
    g_app.new_window_button = CreateWindowExW(0, L"BUTTON", L"New Window",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kNewWindowButton), nullptr, nullptr);
    g_app.status_button = CreateWindowExW(0, L"BUTTON", L"Status On",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kStatusButton), nullptr, nullptr);
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
    g_app.stoch_button = CreateWindowExW(0, L"BUTTON", L"[Stoch 3]",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kStochButton), nullptr, nullptr);
    g_app.stoch_parameters_button = CreateWindowExW(0, L"BUTTON", L"Stoch Params",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kStochParametersButton), nullptr, nullptr);
    g_app.hotkeys_button = CreateWindowExW(0, L"BUTTON", L"Hotkeys",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kHotkeysButton), nullptr, nullptr);
    g_app.tf_1m_button = CreateWindowExW(0, L"BUTTON", L"[1m]",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframe1mButton), nullptr, nullptr);
    g_app.tf_15m_button = CreateWindowExW(0, L"BUTTON", L"15m",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframe15mButton), nullptr, nullptr);
    g_app.tf_30m_button = CreateWindowExW(0, L"BUTTON", L"30m",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframe30mButton), nullptr, nullptr);
    g_app.tf_1h_button = CreateWindowExW(0, L"BUTTON", L"[1h]",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframe1HButton), nullptr, nullptr);
    g_app.tf_2h_button = CreateWindowExW(0, L"BUTTON", L"2h",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframe2HButton), nullptr, nullptr);
    g_app.tf_4h_button = CreateWindowExW(0, L"BUTTON", L"4h",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframe4HButton), nullptr, nullptr);
    g_app.tf_d_button = CreateWindowExW(0, L"BUTTON", L"D",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframeDButton), nullptr, nullptr);
    g_app.tf_w_button = CreateWindowExW(0, L"BUTTON", L"W",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframeWButton), nullptr, nullptr);
    g_app.tf_m_button = CreateWindowExW(0, L"BUTTON", L"M",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTimeframeMButton), nullptr, nullptr);
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
    client.bottom -= ScaleByDpi(kAxisLabelHeight);
    if (g_app.status_visible) {
        client.bottom -= ScaleByDpi(kBottomBarHeight);
    }
    if (g_app.stoch_visible) {
        client.bottom -= ScaleByDpi(g_app.stoch_panel_height + kStochPanelGap);
    }
    return client;
}

RECT StochRect() {
    RECT client{};
    GetClientRect(g_app.window, &client);
    if (g_app.status_visible) {
        client.bottom -= ScaleByDpi(kBottomBarHeight);
    }
    client.top = ChartRect().bottom + ScaleByDpi(kAxisLabelHeight + kStochPanelGap);
    return client;
}

RECT StochResizeRect() {
    if (!g_app.stoch_visible) {
        return RECT{};
    }

    const RECT chart = ChartRect();
    const RECT stoch = StochRect();
    return RECT{chart.left, chart.bottom, chart.right, stoch.top};
}

bool PointInStochResizeZone(POINT point) {
    if (!g_app.stoch_visible) {
        return false;
    }

    RECT zone = StochResizeRect();
    zone.top -= ScaleByDpi(kStochResizeHitHeight / 2);
    zone.bottom += ScaleByDpi(kStochResizeHitHeight / 2);
    return PointInRectStrict(zone, point);
}

void ResizeStochPanelForY(int y) {
    RECT client{};
    GetClientRect(g_app.window, &client);
    const int panel_bottom = client.bottom -
        (g_app.status_visible ? ScaleByDpi(kBottomBarHeight) : 0);
    const int panel_height = panel_bottom - y - ScaleByDpi(kAxisLabelHeight + kStochPanelGap);
    const int max_height = std::max(
        ScaleByDpi(kMinStochPanelHeight),
        static_cast<int>(client.bottom) -
            ScaleByDpi(kTopBarHeight + kAxisLabelHeight + kStochPanelGap) -
            (g_app.status_visible ? ScaleByDpi(kBottomBarHeight) : 0));
    const int next_height = std::clamp(
        panel_height,
        ScaleByDpi(kMinStochPanelHeight),
        std::min(ScaleByDpi(kMaxStochPanelHeight), max_height));
    if (next_height == ScaleByDpi(g_app.stoch_panel_height)) {
        return;
    }

    g_app.stoch_panel_height = std::max(kMinStochPanelHeight,
        MulDiv(next_height, static_cast<int>(kDefaultDpi), static_cast<int>(g_app.dpi)));
    InvalidateChartAndStatus();
}

void InvalidateChartAndStatus() {
    if (g_app.window == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(g_app.window, &client);
    RECT update{
        0,
        ScaleByDpi(kTopBarHeight),
        client.right,
        client.bottom
    };
    InvalidateRect(g_app.window, &update, FALSE);
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
    stream << L"  " << TimeframeLabel(g_app.timeframe)
           << L"  |  Playback " << FormatTimestamp(candle.timestamp)
           << L"  |  O " << candle.open
           << L"  H " << candle.high
           << L"  L " << candle.low
           << L"  C " << candle.close
           << L"  |  " << (g_app.playing ? L"Playing" : L"Paused")
           << L"  x" << static_cast<int>(g_app.speed_options[g_app.speed_index])
           << L"  |  Zoom " << g_app.chart_zoom << L"x"
           << L"  |  Stoch " << (g_app.stoch_visible ? L"3" : L"OFF")
           << L"  Lines " << g_app.trendlines.size();
    std::size_t buy_count = 0;
    std::size_t sell_count = 0;
    for (const TradeMarker& marker : g_app.trade_markers) {
        if (marker.buy) {
            ++buy_count;
        } else {
            ++sell_count;
        }
    }
    stream << L"  |  Buy " << buy_count << L"  Sell " << sell_count;
    const ChartView view = BuildCurrentChartView();
    if (view.valid && view.end >= 0 && view.end < static_cast<int>(candles.size())) {
        stream << L"  |  View " << FormatTimestamp(candles[static_cast<std::size_t>(view.end)].timestamp);
    }
    if (g_app.trendline_draft_active) {
        stream << L"  |  Trendline: click second point";
    } else if (g_app.trendline_mode) {
        stream << L"  |  LClick draw  Del undo";
    } else if (g_app.chart_drag_active || !g_app.chart_follow_playback) {
        stream << L"  |  Drag: pan view";
    } else {
        stream << L"  |  Drag pan  Wheel zoom";
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

void DrawTradeMarker(HDC dc, const ChartView& view, const TradeMarker& marker) {
    if (!view.valid || view.candles == nullptr) {
        return;
    }

    const int x = TimestampToX(view, marker.timestamp);
    const int price_y = PriceToY(marker.price, view.min_price, view.max_price, view.rect);
    const int length = ScaleByDpi(18);
    const int half_width = ScaleByDpi(6);
    const COLORREF color = marker.buy ? RGB(0, 210, 105) : RGB(245, 80, 80);
    const int tip_y = price_y;
    const int base_y = marker.buy ? price_y + length : price_y - length;

    HPEN pen = CreatePen(PS_SOLID, std::max(1, ScaleByDpi(2)), color);
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    MoveToEx(dc, x, base_y, nullptr);
    LineTo(dc, x, marker.buy ? tip_y + half_width : tip_y - half_width);
    const POINT arrow[] = {
        {x, tip_y},
        {x - half_width, marker.buy ? tip_y + half_width : tip_y - half_width},
        {x + half_width, marker.buy ? tip_y + half_width : tip_y - half_width},
    };
    Polygon(dc, arrow, 3);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);

    RECT label_rect{
        x - ScaleByDpi(24),
        marker.buy ? base_y + ScaleByDpi(1) : base_y - ScaleByDpi(17),
        x + ScaleByDpi(24),
        marker.buy ? base_y + ScaleByDpi(17) : base_y - ScaleByDpi(1)
    };
    DrawTextInRect(dc, label_rect, marker.buy ? L"BUY" : L"SELL", color,
        g_app.small_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawTradeMarkers(HDC dc, const ChartView& view) {
    for (const TradeMarker& marker : g_app.trade_markers) {
        DrawTradeMarker(dc, view, marker);
    }
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
    // Include the dedicated time-axis strip below the price plot. Candle and
    // trendline geometry itself remains inside view.rect.
    IntersectClipRect(dc, view.rect.left, view.rect.top, view.rect.right,
        view.rect.bottom + ScaleByDpi(kAxisLabelHeight));

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

    DrawTradeMarkers(dc, view);

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

int StochValueToY(double value, const RECT& rect) {
    const double clamped = std::clamp(value, 0.0, 100.0);
    const double ratio = (100.0 - clamped) / 100.0;
    return rect.top + static_cast<int>(std::lround(ratio * static_cast<double>(rect.bottom - rect.top)));
}

void DrawStochLine(HDC dc, const ChartView& view, const std::vector<double>& values,
                   const RECT& stoch_rect, COLORREF color, int width) {
    if (!view.valid || values.empty()) {
        return;
    }

    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    bool has_previous = false;
    POINT previous{};
    for (int i = view.start; i <= view.end && i < static_cast<int>(values.size()); ++i) {
        const double value = values[static_cast<std::size_t>(i)];
        if (!std::isfinite(value)) {
            has_previous = false;
            continue;
        }

        const int x = view.rect.left + static_cast<int>(
            std::lround((static_cast<double>(i - view.start) + 0.5) * view.step));
        const POINT current{x, StochValueToY(value, stoch_rect)};
        if (has_previous) {
            MoveToEx(dc, previous.x, previous.y, nullptr);
            LineTo(dc, current.x, current.y);
        }
        previous = current;
        has_previous = true;
    }

    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void DrawStoch(HDC dc) {
    if (!g_app.stoch_visible) {
        return;
    }

    const RECT stoch_rect = StochRect();
    const ChartView view = BuildCurrentChartView();
    if (!view.valid || stoch_rect.bottom <= stoch_rect.top) {
        return;
    }

    SaveDC(dc);
    IntersectClipRect(dc, stoch_rect.left, stoch_rect.top, stoch_rect.right, stoch_rect.bottom);

    HPEN reference_pen = CreatePen(PS_DOT, std::max(1, ScaleByDpi(1)), RGB(90, 90, 98));
    HGDIOBJ old_pen = SelectObject(dc, reference_pen);
    for (const double level : {20.0, 50.0, 80.0}) {
        const int y = StochValueToY(level, stoch_rect);
        MoveToEx(dc, stoch_rect.left, y, nullptr);
        LineTo(dc, stoch_rect.right, y);
    }
    SelectObject(dc, old_pen);
    DeleteObject(reference_pen);

    const std::array<COLORREF, 5> k_colors{
        RGB(255, 255, 255), RGB(255, 0, 0), RGB(255, 255, 255),
        RGB(128, 0, 128), RGB(0, 180, 0)
    };
    const std::array<COLORREF, 5> d_colors{
        RGB(255, 255, 0), RGB(0, 255, 255), RGB(255, 255, 0),
        RGB(255, 165, 0), RGB(0, 120, 255)
    };
    const std::array<int, 5> widths{
        2, 2, 3, 4, 5
    };
    constexpr std::size_t kVisibleStochGroups = 3;
    const std::size_t group_count = kVisibleStochGroups;
    for (std::size_t group = 0; group < group_count; ++group) {
        DrawStochLine(dc, view, g_app.stoch_series.k[group], stoch_rect,
            k_colors[group], std::max(1, ScaleByDpi(widths[group])));
        DrawStochLine(dc, view, g_app.stoch_series.d[group], stoch_rect,
            d_colors[group], std::max(1, ScaleByDpi(widths[group])));
    }

    RestoreDC(dc, -1);

    RECT title_rect{
        stoch_rect.left + ScaleByDpi(6), stoch_rect.top + ScaleByDpi(2),
        stoch_rect.left + ScaleByDpi(250), stoch_rect.top + ScaleByDpi(18)
    };
    DrawTextInRect(dc, title_rect, L"stoch_btc_v9_k5_optimized",
        RGB(175, 175, 180), g_app.small_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    for (const int level : {100, 80, 50, 20, 0}) {
        const int y = StochValueToY(static_cast<double>(level), stoch_rect);
        RECT label_rect{
            stoch_rect.right - ScaleByDpi(38),
            std::max(static_cast<int>(stoch_rect.top), y - ScaleByDpi(8)),
            stoch_rect.right - ScaleByDpi(4),
            std::min(static_cast<int>(stoch_rect.bottom), y + ScaleByDpi(8))
        };
        std::wostringstream label;
        label << level;
        DrawTextInRect(dc, label_rect, label.str(), RGB(135, 135, 142), g_app.small_font,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawStochResizeHandle(HDC dc) {
    if (!g_app.stoch_visible) {
        return;
    }

    const RECT stoch = StochRect();
    const int y = stoch.top - ScaleByDpi(kStochPanelGap / 2);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, ScaleByDpi(1)), RGB(70, 70, 78));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, stoch.left, y, nullptr);
    LineTo(dc, stoch.right, y);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    const int handle_width = ScaleByDpi(34);
    const int handle_height = ScaleByDpi(3);
    RECT handle{
        (stoch.left + stoch.right - handle_width) / 2,
        y - handle_height / 2,
        (stoch.left + stoch.right + handle_width) / 2,
        y + handle_height / 2 + 1
    };
    HBRUSH brush = CreateSolidBrush(RGB(150, 150, 158));
    FillRect(dc, &handle, brush);
    DeleteObject(brush);
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
    DrawStoch(memory_dc);
    DrawStochResizeHandle(memory_dc);

    if (g_app.status_visible) {
        RECT status_bar{
            0,
            client.bottom - ScaleByDpi(kBottomBarHeight),
            client.right,
            client.bottom
        };
        HBRUSH status_background = CreateSolidBrush(RGB(18, 18, 22));
        FillRect(memory_dc, &status_bar, status_background);
        DeleteObject(status_background);

        HPEN separator_pen = CreatePen(PS_SOLID, std::max(1, ScaleByDpi(1)), RGB(62, 62, 70));
        HGDIOBJ old_pen = SelectObject(memory_dc, separator_pen);
        MoveToEx(memory_dc, status_bar.left, status_bar.top, nullptr);
        LineTo(memory_dc, status_bar.right, status_bar.top);
        SelectObject(memory_dc, old_pen);
        DeleteObject(separator_pen);

        RECT status_text{
            ScaleByDpi(10),
            status_bar.top + ScaleByDpi(2),
            status_bar.right - ScaleByDpi(10),
            status_bar.bottom - ScaleByDpi(3)
        };
        DrawTextInRect(memory_dc, status_text, BuildStatusLine(), RGB(220, 220, 220), g_app.ui_font,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

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
    if (g_app.chart_follow_playback) {
        g_app.chart_end_index = next_index;
    }
    if (g_app.playback_index >= candles.size() - 1) {
        StopPlayback();
    }

    if (!g_app.dragging_slider) {
        SendMessageW(g_app.progress_slider, TBM_SETPOS, TRUE, static_cast<LPARAM>(g_app.playback_index));
    }
    InvalidateChartAndStatus();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
        case WM_CREATE:
            g_app.window = window;
            g_app.dpi = GetWindowDpi(window);
            InitializeStochParameters();
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
                case kNewWindowButton:
                    OpenNewWindow();
                    return 0;
                case kStatusButton:
                    g_app.status_visible = !g_app.status_visible;
                    LayoutControls();
                    RefreshUiState();
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
                case kStochButton:
                    CycleStochVisibility();
                    return 0;
                case kStochParametersButton:
                    ShowSettingsDialog(false);
                    return 0;
                case kHotkeysButton:
                    ShowSettingsDialog(true);
                    return 0;
                case kTimeframe1mButton:
                    StopPlayback();
                    SetTimeframe(Timeframe::M1);
                    return 0;
                case kTimeframe15mButton:
                    StopPlayback();
                    SetTimeframe(Timeframe::M15);
                    return 0;
                case kTimeframe30mButton:
                    StopPlayback();
                    SetTimeframe(Timeframe::M30);
                    return 0;
                case kTimeframe1HButton:
                    StopPlayback();
                    SetTimeframe(Timeframe::H1);
                    return 0;
                case kTimeframe2HButton:
                    StopPlayback();
                    SetTimeframe(Timeframe::H2);
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
                case kTimeframeMButton:
                    StopPlayback();
                    SetTimeframe(Timeframe::MN1);
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
                g_app.chart_follow_playback = true;
                g_app.chart_end_index = next_index;
                if (!candles.empty()) {
                    g_app.playback_timestamp = candles[std::min(next_index, candles.size() - 1)].timestamp;
                }
                InvalidateChartAndStatus();

                if (code == TB_ENDTRACK || code == SB_ENDSCROLL || code == TB_THUMBPOSITION) {
                    g_app.dragging_slider = false;
                }
                return 0;
            }
            break;

        case WM_LBUTTONDOWN: {
            SetFocus(window);
            const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            if (PointInStochResizeZone(point)) {
                g_app.stoch_resize_active = true;
                SetCapture(window);
                ResizeStochPanelForY(point.y);
                return 0;
            }
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
            if (view.valid && PointInRectStrict(view.rect, point)) {
                g_app.chart_drag_active = true;
                g_app.chart_drag_start = point;
                g_app.chart_drag_start_end = static_cast<std::size_t>(view.end);
                g_app.chart_follow_playback = false;
                SetCapture(window);
                return 0;
            }
            break;
        }

        case WM_MOUSEMOVE:
            if (g_app.stoch_resize_active) {
                ResizeStochPanelForY(GET_Y_LPARAM(l_param));
                return 0;
            }
            if (g_app.chart_drag_active) {
                const ChartView view = BuildCurrentChartView();
                const std::vector<Candle>& candles = CurrentCandles();
                if (view.valid && !candles.empty()) {
                    const int delta_x = GET_X_LPARAM(l_param) - g_app.chart_drag_start.x;
                    const int delta_slots = static_cast<int>(std::lround(
                        static_cast<double>(delta_x) / std::max(view.step, 1.0)));
                    const int last = static_cast<int>(candles.size() - 1);
                    const int next_end = std::clamp(
                        static_cast<int>(g_app.chart_drag_start_end) - delta_slots, 0, last);
                    g_app.chart_end_index = static_cast<std::size_t>(next_end);
                    InvalidateChartAndStatus();
                }
                return 0;
            }
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

        case WM_LBUTTONUP:
            if (g_app.stoch_resize_active) {
                g_app.stoch_resize_active = false;
                ReleaseCapture();
                return 0;
            }
            if (g_app.chart_drag_active) {
                g_app.chart_drag_active = false;
                ReleaseCapture();
                return 0;
            }
            break;

        case WM_SETCURSOR:
            if (g_app.stoch_resize_active || g_app.chart_drag_active ||
                (LOWORD(l_param) == HTCLIENT && !g_app.trendline_draft_active)) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                if (g_app.stoch_resize_active || PointInStochResizeZone(point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                    return TRUE;
                }
                if (g_app.chart_drag_active ||
                    (!g_app.trendline_mode && PointInRectStrict(ChartRect(), point))) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
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

        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            ScreenToClient(window, &point);
            if (PointInRectStrict(ChartRect(), point) ||
                (g_app.stoch_visible && PointInRectStrict(StochRect(), point))) {
                AdjustChartZoom(GET_WHEEL_DELTA_WPARAM(w_param));
                return 0;
            }
            break;
        }

        case WM_KEYDOWN:
            if (HandleConfiguredShortcut(static_cast<UINT>(w_param))) {
                return 0;
            }
            switch (w_param) {
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
            if (g_app.stoch_resize_active && reinterpret_cast<HWND>(l_param) != window) {
                g_app.stoch_resize_active = false;
            }
            if (g_app.chart_drag_active && reinterpret_cast<HWND>(l_param) != window) {
                g_app.chart_drag_active = false;
            }
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
