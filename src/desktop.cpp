#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include "desktop.h"
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace desktop {
namespace {
constexpr wchar_t kSelectorClass[] = L"ColorTileScreenSelector";
constexpr wchar_t kHintClass[] = L"ColorTileNextMoveHint";
constexpr UINT_PTR kPulseTimer = 1;
constexpr uint64_t kMaxCapturePixels = 100000000;
HWND hint_window = nullptr;
RECT hint_cell{}, hint_bounds{};
bool hint_visible = false;

RECT virtual_screen() {
    const LONG x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const LONG y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return {x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN), y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}
bool nonempty(const RECT& r) { return r.right > r.left && r.bottom > r.top; }
bool equal_rect(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
bool contains_rect(const RECT& outer, const RECT& inner) {
    return nonempty(inner) && inner.left >= outer.left && inner.top >= outer.top &&
           inner.right <= outer.right && inner.bottom <= outer.bottom;
}
HWND root_window(HWND w) { return w ? GetAncestor(w, GA_ROOT) : nullptr; }
bool is_desktop_window(HWND w) {
    wchar_t name[128]{};
    GetClassNameW(w, name, static_cast<int>(std::size(name)));
    return w == GetDesktopWindow() || wcscmp(name, L"Progman") == 0 ||
           wcscmp(name, L"WorkerW") == 0 || wcscmp(name, L"Shell_TrayWnd") == 0 ||
           wcscmp(name, L"Shell_SecondaryTrayWnd") == 0;
}
HMODULE dwm_module() {
    static HMODULE module = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module;
}
void flush_desktop() {
    GdiFlush();
    using Fn = HRESULT (WINAPI*)();
    static Fn flush = reinterpret_cast<Fn>(dwm_module() ? GetProcAddress(dwm_module(), "DwmFlush") : nullptr);
    if (flush) flush();
}
bool is_cloaked(HWND w) {
    using Fn = HRESULT (WINAPI*)(HWND, DWORD, PVOID, DWORD);
    static Fn get = reinterpret_cast<Fn>(dwm_module() ? GetProcAddress(dwm_module(), "DwmGetWindowAttribute") : nullptr);
    DWORD cloaked = 0;
    return get && SUCCEEDED(get(w, 14 /* DWMWA_CLOAKED */, &cloaked, sizeof(cloaked))) && cloaked;
}
struct HintPause {
    bool restore = hint_window && hint_visible && IsWindowVisible(hint_window);
    HintPause() { if (restore) { ShowWindow(hint_window, SW_HIDE); flush_desktop(); } }
    ~HintPause() {
        if (restore && hint_window && hint_visible)
            SetWindowPos(hint_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
};

bool capture_rectangle(RECT r, ct::Image& image, std::wstring& error) {
    const int64_t width = static_cast<int64_t>(r.right) - r.left;
    const int64_t height = static_cast<int64_t>(r.bottom) - r.top;
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768 ||
        static_cast<uint64_t>(width) * height > kMaxCapturePixels) {
        error = L"画面の範囲が大きすぎるか、範囲を取得できません。";
        return false;
    }
    HDC screen = GetDC(nullptr);
    HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = static_cast<LONG>(width);
    bi.bmiHeader.biHeight = -static_cast<LONG>(height);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = screen ? CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    HGDIOBJ old = memory && bitmap ? SelectObject(memory, bitmap) : nullptr;
    bool copied = memory && bitmap && bits && old && old != HGDI_ERROR &&
                  BitBlt(memory, 0, 0, static_cast<int>(width), static_cast<int>(height), screen,
                         r.left, r.top, SRCCOPY | CAPTUREBLT);
    if (copied) {
        GdiFlush();
        ct::Image result;
        result.width = static_cast<int>(width); result.height = static_cast<int>(height);
        result.rgb.resize(static_cast<size_t>(width * height) * 3);
        const auto* pixels = static_cast<const uint8_t*>(bits);
        for (size_t i = 0; i < static_cast<size_t>(width * height); ++i) {
            result.rgb[i * 3] = pixels[i * 4 + 2];
            result.rgb[i * 3 + 1] = pixels[i * 4 + 1];
            result.rgb[i * 3 + 2] = pixels[i * 4];
        }
        image = std::move(result);
    }
    if (old && old != HGDI_ERROR) SelectObject(memory, old);
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(nullptr, screen);
    if (!copied) error = L"画面を取得できませんでした。ゲームを通常のウィンドウで表示してください。";
    return copied;
}

struct AboveCheck { const Target* target; bool found = false; bool covered = false; };
BOOL CALLBACK check_above(HWND w, LPARAM param) {
    auto& check = *reinterpret_cast<AboveCheck*>(param);
    if (w == check.target->window) { check.found = true; return FALSE; }
    if (w == hint_window || !IsWindowVisible(w) || IsIconic(w) || is_cloaked(w)) return TRUE;
    RECT r{}, overlap{};
    if (GetWindowRect(w, &r) && IntersectRect(&overlap, &r, &check.target->selection)) {
        check.covered = true;
        return FALSE;
    }
    return TRUE;
}
bool check_identity(const Target& target, std::wstring& error) {
    DWORD pid = 0;
    if (!target.window || !IsWindow(target.window) || root_window(target.window) != target.window ||
        !GetWindowThreadProcessId(target.window, &pid) || !pid || pid != target.process_id ||
        pid == GetCurrentProcessId() || is_desktop_window(target.window)) {
        error = L"指定したゲームのウィンドウを確認できません。盤面を指定し直してください。";
        return false;
    }
    return true;
}

struct Selector {
    HWND window = nullptr;
    RECT desktop{};
    ct::Image background;
    std::vector<uint8_t> bgra;
    POINT start{}, current{};
    RECT chosen{};
    bool dragging = false, accepted = false, done = false;
    HFONT font = nullptr;
};
RECT drag_rectangle(const Selector& s) {
    return {std::min(s.start.x, s.current.x), std::min(s.start.y, s.current.y),
            std::max(s.start.x, s.current.x), std::max(s.start.y, s.current.y)};
}
POINT selector_point(const Selector& s, LPARAM position) {
    // Preserve the location at the time this mouse message was generated.
    // GetCursorPos here could already point at the end of a queued fast drag.
    // The captured desktop is limited to 32768 pixels per axis, so every
    // on-screen client coordinate fits the message's signed 16-bit values.
    return {std::clamp<LONG>(GET_X_LPARAM(position), 0, s.background.width),
            std::clamp<LONG>(GET_Y_LPARAM(position), 0, s.background.height)};
}
void paint_selector(Selector& s) {
    PAINTSTRUCT ps{}; HDC dc = BeginPaint(s.window, &ps);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = s.background.width; bi.bmiHeader.biHeight = -s.background.height;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, s.background.width, s.background.height, 0, 0, 0,
                     s.background.height, s.bgra.data(), &bi, DIB_RGB_COLORS);
    RECT selection = drag_rectangle(s);
    HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
    HGDIOBJ old_brush = SelectObject(dc, hollow);
    HPEN pen = CreatePen(PS_SOLID, 3, RGB(35, 136, 255));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    if (s.dragging) Rectangle(dc, selection.left, selection.top, selection.right, selection.bottom);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(pen);
    // Keep the instruction near the current monitor, including monitors whose
    // desktop origin is negative. This is the only text drawn over the snapshot.
    POINT cursor{}; GetCursorPos(&cursor);
    MONITORINFO info{}; info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &info);
    RECT label{info.rcMonitor.left - s.desktop.left + 20,
               info.rcMonitor.top - s.desktop.top + 20,
               info.rcMonitor.left - s.desktop.left + 650,
               info.rcMonitor.top - s.desktop.top + 72};
    HBRUSH panel = CreateSolidBrush(RGB(25, 37, 58)); FillRect(dc, &label, panel); DeleteObject(panel);
    InflateRect(&label, -12, -8);
    const auto old_font = SelectObject(dc, s.font);
    SetTextColor(dc, RGB(255, 255, 255)); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, L"盤面の外枠をドラッグで囲んでください。Escで取り消し", -1, &label,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font); EndPaint(s.window, &ps);
}
LRESULT CALLBACK selector_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<Selector*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        s = static_cast<Selector*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        s->window = w; SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (!s) return DefWindowProcW(w, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint_selector(*s); return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { s->done = true; if (GetCapture() == w) ReleaseCapture(); ShowWindow(w, SW_HIDE); }
        return 0;
    case WM_LBUTTONDOWN:
        s->start = s->current = selector_point(*s, lp);
        s->dragging = true; SetCapture(w); InvalidateRect(w, nullptr, FALSE); return 0;
    case WM_MOUSEMOVE:
        if (s->dragging) {
            s->current = selector_point(*s, lp);
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (s->dragging) {
            s->current = selector_point(*s, lp);
            s->chosen = drag_rectangle(*s); s->dragging = false;
            if (GetCapture() == w) ReleaseCapture();
            if (s->chosen.right - s->chosen.left >= 24 && s->chosen.bottom - s->chosen.top >= 24) {
                s->accepted = s->done = true; ShowWindow(w, SW_HIDE);
            } else InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_RBUTTONDOWN: case WM_CLOSE:
        s->done = true; if (GetCapture() == w) ReleaseCapture(); ShowWindow(w, SW_HIDE); return 0;
    case WM_DISPLAYCHANGE:
        s->done = true; s->accepted = false; ShowWindow(w, SW_HIDE); return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && !s->done) {
            s->done = true; s->accepted = false;
            if (GetCapture() == w) ReleaseCapture();
            ShowWindow(w, SW_HIDE);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (s->dragging) { s->dragging = false; InvalidateRect(w, nullptr, FALSE); }
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

bool render_hint() {
    if (!hint_window || !nonempty(hint_bounds)) return false;
    const int w = hint_bounds.right - hint_bounds.left, h = hint_bounds.bottom - hint_bounds.top;
    HDC screen = GetDC(nullptr), memory = screen ? CreateCompatibleDC(screen) : nullptr;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* raw = nullptr;
    HBITMAP bitmap = screen ? CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &raw, nullptr, 0) : nullptr;
    auto old = memory && bitmap ? SelectObject(memory, bitmap) : nullptr;
    bool drawn = false;
    if (screen && memory && bitmap && raw && old && old != HGDI_ERROR) {
        auto* pixels = static_cast<uint8_t*>(raw);
        const double cx = (hint_cell.left + hint_cell.right) * 0.5 - hint_bounds.left;
        const double cy = (hint_cell.top + hint_cell.bottom) * 0.5 - hint_bounds.top;
        const double rx = std::max(2.0, (hint_cell.right - hint_cell.left) * 0.40);
        const double ry = std::max(2.0, (hint_cell.bottom - hint_cell.top) * 0.40);
        const double pulse = 0.82 + 0.18 * std::sin(GetTickCount64() * 0.007);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry;
            const double distance = (std::sqrt(dx * dx + dy * dy) - 1.0) * std::min(rx, ry);
            const int alpha = std::clamp(static_cast<int>((205 * std::exp(-distance * distance / 3.5) +
                                                          48 * std::exp(-distance * distance / 30)) * pulse), 0, 255);
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            pixels[i] = static_cast<uint8_t>(30 * alpha / 255);
            pixels[i + 1] = static_cast<uint8_t>(220 * alpha / 255);
            pixels[i + 2] = static_cast<uint8_t>(255 * alpha / 255);
            pixels[i + 3] = static_cast<uint8_t>(alpha);
        }
        POINT dst{hint_bounds.left, hint_bounds.top}, src{0, 0}; SIZE size{w, h};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        drawn = UpdateLayeredWindow(hint_window, screen, &dst, &size, memory, &src, 0, &blend, ULW_ALPHA) != 0;
    }
    if (old && old != HGDI_ERROR) SelectObject(memory, old);
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(nullptr, screen);
    return drawn;
}
LRESULT CALLBACK hint_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: if (wp == kPulseTimer && hint_visible && !render_hint()) hide_hint(); return 0;
    case WM_CLOSE: hide_hint(); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
bool register_class(const wchar_t* name, WNDPROC proc, HCURSOR cursor) {
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = proc; wc.lpszClassName = name; wc.hCursor = cursor;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
} // namespace

bool validate(const Target& target, std::wstring& error, bool require_foreground) {
    error.clear();
    if (!check_identity(target, error)) return false;
    RECT current{};
    if (!IsWindowVisible(target.window) || IsIconic(target.window) || is_cloaked(target.window) ||
        !GetWindowRect(target.window, &current) || !equal_rect(current, target.window_rect)) {
        error = L"ゲームのウィンドウが移動、最小化、または非表示になったため停止しました。";
        return false;
    }
    const RECT screen = virtual_screen();
    if ((nonempty(target.virtual_screen) && !equal_rect(screen, target.virtual_screen)) ||
        !contains_rect(screen, target.selection)) {
        error = L"ディスプレイの配置が変わったか、盤面が画面外にあるため停止しました。";
        return false;
    }
    RECT client{}; GetClientRect(target.window, &client);
    POINT corners[2]{{client.left, client.top}, {client.right, client.bottom}};
    if (!ClientToScreen(target.window, &corners[0]) || !ClientToScreen(target.window, &corners[1]) ||
        !contains_rect(RECT{corners[0].x, corners[0].y, corners[1].x, corners[1].y}, target.selection)) {
        error = L"盤面の範囲がゲームの表示領域から外れています。範囲を指定し直してください。";
        return false;
    }
    if (require_foreground && root_window(GetForegroundWindow()) != target.window) {
        error = L"別のウィンドウが操作対象になったため停止しました。";
        return false;
    }
    HintPause pause;
    AboveCheck above{&target}; EnumWindows(check_above, reinterpret_cast<LPARAM>(&above));
    if (above.covered || !above.found) {
        error = L"盤面に別のウィンドウが重なったため停止しました。";
        return false;
    }
    const auto& r = target.selection;
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x) {
        POINT point{r.left + (r.right - r.left - 1) * x / 2,
                    r.top + (r.bottom - r.top - 1) * y / 2};
        if (root_window(WindowFromPoint(point)) != target.window) {
            error = L"指定した盤面を画面上で確認できないため停止しました。";
            return false;
        }
    }
    return true;
}

bool capture(const Target& target, ct::Image& image, std::wstring& error, bool require_foreground) {
    HintPause pause;
    if (!validate(target, error, require_foreground)) return false;
    if (!capture_rectangle(target.selection, image, error)) return false;
    // Check again after the capture so a window transition cannot silently
    // replace the screenshot with unrelated content.
    return validate(target, error, require_foreground);
}

bool focus_target(const Target& target, std::wstring& error) {
    error.clear();
    if (!check_identity(target, error)) return false;
    if (IsIconic(target.window)) ShowWindow(target.window, SW_RESTORE);
    SetForegroundWindow(target.window);
    // A different input queue processes foreground activation asynchronously.
    // Let that queue finish the activation request before inspecting it. This
    // is a bounded acknowledgment, not a second request or a focus restriction
    // bypass. An unresponsive, destroyed or blocked target still fails closed.
    // https://devblogs.microsoft.com/oldnewthing/20161118-00/?p=94745
    DWORD_PTR acknowledgment = 0;
    if (!SendMessageTimeoutW(target.window, WM_NULL, 0, 0,
                             SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 500, &acknowledgment)) {
        error = L"ゲームの応答を確認できなかったため、画面操作を開始しませんでした。";
        return false;
    }
    flush_desktop();
    if (root_window(GetForegroundWindow()) != target.window) {
        error = L"ゲームを前面にできませんでした。ゲームをクリックしてから開始してください。";
        return false;
    }
    return validate(target, error, true);
}

bool click_cell(const Target& target, POINT point, std::wstring& error) {
    HintPause pause;
    if (!PtInRect(&target.selection, point)) {
        error = L"クリック位置が指定した盤面の外にあるため停止しました。";
        return false;
    }
    if (!validate(target, error, true)) return false;
    if (root_window(WindowFromPoint(point)) != target.window) {
        error = L"クリック位置に別のウィンドウがあるため停止しました。";
        return false;
    }
    for (int key : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2,
                    VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN, VK_ESCAPE, VK_F8}) {
        if (GetAsyncKeyState(key) & 0x8000) {
            error = L"停止キー、マウスボタン、または修飾キーが押されているため停止しました。";
            return false;
        }
    }
    const RECT screen = virtual_screen();
    const int64_t width = static_cast<int64_t>(screen.right) - screen.left;
    const int64_t height = static_cast<int64_t>(screen.bottom) - screen.top;
    if (width < 2 || height < 2) { error = L"画面の座標を取得できません。"; return false; }
    INPUT events[3]{};
    for (auto& input : events) input.type = INPUT_MOUSE;
    // Aim at the center of a physical pixel's normalized interval. Using only
    // its left edge can round one pixel backward on high-resolution monitors.
    events[0].mi.dx = static_cast<LONG>(((static_cast<int64_t>(point.x) - screen.left) * 65536 + 32768) / width);
    events[0].mi.dy = static_cast<LONG>(((static_cast<int64_t>(point.y) - screen.top) * 65536 + 32768) / height);
    events[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    events[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    events[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    const UINT sent = SendInput(3, events, sizeof(INPUT));
    if (sent != 3) {
        // If Windows accepted the down event but not its matching up, release
        // only that button. Never retry the click or inject another down event.
        if (sent == 2) {
            INPUT release{}; release.type = INPUT_MOUSE; release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &release, sizeof(INPUT));
        }
        error = L"Windowsがクリック入力を受け付けなかったため停止しました。ゲームを通常権限で起動してください。";
        return false;
    }
    return true;
}

bool select_region(HWND owner, Target& target, ct::Image& image, std::wstring& error) {
    error.clear(); hide_hint();
    if (!register_class(kSelectorClass, selector_proc, LoadCursorW(nullptr, IDC_CROSS))) {
        error = L"範囲指定画面を作成できません。"; return false;
    }
    WINDOWPLACEMENT placement{}; placement.length = sizeof(placement);
    const bool have_placement = owner && GetWindowPlacement(owner, &placement);
    const bool visible = owner && IsWindowVisible(owner);
    const bool enabled = owner && IsWindowEnabled(owner);
    if (owner) { EnableWindow(owner, FALSE); ShowWindow(owner, SW_HIDE); }
    flush_desktop();
    // Uncovering another process's window can leave a pending repaint after
    // the compositor flush returns. Give that window a short, bounded settling
    // period before freezing the selection background.
    Sleep(120);
    flush_desktop();
    Selector s; s.desktop = virtual_screen();
    bool ok = capture_rectangle(s.desktop, s.background, error);
    if (ok) {
        s.bgra.resize(static_cast<size_t>(s.background.width) * s.background.height * 4);
        for (size_t i = 0; i < s.background.rgb.size() / 3; ++i) {
            s.bgra[i * 4] = s.background.rgb[i * 3 + 2];
            s.bgra[i * 4 + 1] = s.background.rgb[i * 3 + 1];
            s.bgra[i * 4 + 2] = s.background.rgb[i * 3];
            s.bgra[i * 4 + 3] = 255;
        }
        s.font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
        HWND w = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kSelectorClass, L"盤面の範囲を指定",
            WS_POPUP, s.desktop.left, s.desktop.top, s.background.width, s.background.height,
            nullptr, nullptr, GetModuleHandleW(nullptr), &s);
        if (!w) { error = L"範囲指定画面を作成できません。"; ok = false; }
        else {
            ShowWindow(w, SW_SHOW); SetForegroundWindow(w); SetFocus(w); UpdateWindow(w);
            MSG msg{};
            while (!s.done) {
                const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
                if (got <= 0) { if (!got) PostQuitMessage(static_cast<int>(msg.wParam)); break; }
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (GetCapture() == w) ReleaseCapture();
            ShowWindow(w, SW_HIDE); DestroyWindow(w); flush_desktop();
            ok = s.accepted;
            if (ok) {
                Sleep(80);
                flush_desktop();
                Target selected;
                selected.selection = {s.desktop.left + s.chosen.left, s.desktop.top + s.chosen.top,
                                      s.desktop.left + s.chosen.right, s.desktop.top + s.chosen.bottom};
                selected.virtual_screen = s.desktop;
                POINT center{selected.selection.left + (selected.selection.right - selected.selection.left) / 2,
                             selected.selection.top + (selected.selection.bottom - selected.selection.top) / 2};
                selected.window = root_window(WindowFromPoint(center));
                GetWindowThreadProcessId(selected.window, &selected.process_id);
                GetWindowRect(selected.window, &selected.window_rect);
                // The frozen background is only a selection aid. Return a
                // fresh board so a long drag or delayed uncover repaint does
                // not bind the solver to an old frame. capture performs both
                // before/after identity, geometry and occlusion validation.
                ct::Image current;
                if (!capture(selected, current, error, false)) ok = false;
                else { target = selected; image = std::move(current); }
            }
        }
        if (s.font) DeleteObject(s.font);
    }
    if (owner && IsWindow(owner)) {
        EnableWindow(owner, enabled);
        if (visible) {
            if (have_placement) SetWindowPlacement(owner, &placement);
            ShowWindow(owner, have_placement ? static_cast<int>(placement.showCmd) : SW_SHOWNOACTIVATE);
            if (!IsIconic(owner)) SetForegroundWindow(owner);
        }
    }
    return ok;
}

bool show_hint(const Target& target, RECT screen_cell, int step, int total) {
    if (!contains_rect(target.selection, screen_cell) || screen_cell.right - screen_cell.left > 2048 ||
        screen_cell.bottom - screen_cell.top > 2048) return false;
    std::wstring error;
    if (!validate(target, error, true)) { hide_hint(); return false; }
    if (!hint_window) {
        if (!register_class(kHintClass, hint_proc, LoadCursorW(nullptr, IDC_ARROW))) return false;
        hint_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            kHintClass, L"次の手", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hint_window) return false;
    }
    hint_cell = screen_cell; hint_bounds = screen_cell;
    InflateRect(&hint_bounds, 5, 5);
    IntersectRect(&hint_bounds, &hint_bounds, &target.selection);
    const std::wstring title = L"次の手 " + std::to_wstring(step) + L" / " + std::to_wstring(total);
    SetWindowTextW(hint_window, title.c_str());
    hint_visible = true;
    if (!render_hint()) { hide_hint(); return false; }
    SetWindowPos(hint_window, HWND_TOPMOST, hint_bounds.left, hint_bounds.top,
        hint_bounds.right - hint_bounds.left, hint_bounds.bottom - hint_bounds.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(hint_window, kPulseTimer, 70, nullptr);
    return IsWindowVisible(hint_window) != 0;
}
void hide_hint() {
    hint_visible = false;
    if (hint_window) { KillTimer(hint_window, kPulseTimer); ShowWindow(hint_window, SW_HIDE); }
}
void destroy_hint() {
    hide_hint();
    if (hint_window) { DestroyWindow(hint_window); hint_window = nullptr; }
}
} // namespace desktop
