// End-to-end GUI regression. It starts its own mock game and application and
// injects input only while one of these private test windows is foreground.
// Run in an isolated desktop, not alongside a real game or other user work.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int kLimit = 107, kSearchStop = 109, kRoute = 114, kMode = 118, kInterval = 119;
constexpr int kScreenAuto = 121, kScreenHint = 122, kLiveStop = 123;
const wchar_t* const kClicks = L"ColorTileMockClickCount";
const wchar_t* const kRemaining = L"ColorTileMockRemaining";
const wchar_t* const kOutside = L"ColorTileMockOutsideClicks";
constexpr wchar_t kAppClass[] = L"ColorTileSolverWindow";
constexpr wchar_t kMockClass[] = L"ColorTileSolverMockGame";
constexpr wchar_t kSelectorClass[] = L"ColorTileScreenSelector";
constexpr wchar_t kHintClass[] = L"ColorTileNextMoveHint";

void pump() { MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
bool wait_for(const std::function<bool()>& predicate, DWORD timeout = 5000) {
    const ULONGLONG until = GetTickCount64() + timeout;
    do { pump(); if (predicate()) return true; Sleep(15); } while (GetTickCount64() < until);
    pump(); return predicate();
}
void settle(DWORD ms) {
    const ULONGLONG until = GetTickCount64() + ms;
    while (GetTickCount64() < until) { pump(); Sleep(15); }
}
struct FindState { DWORD pid = 0; const wchar_t* name = nullptr; HWND found = nullptr; };
BOOL CALLBACK find_window(HWND hwnd, LPARAM lp) {
    auto& state = *reinterpret_cast<FindState*>(lp); DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    wchar_t name[128]{}; GetClassNameW(hwnd, name, 128);
    if (pid == state.pid && std::wstring(name) == state.name) { state.found = hwnd; return FALSE; }
    return TRUE;
}
HWND find(DWORD pid, const wchar_t* name) {
    FindState state{pid, name}; EnumWindows(find_window, reinterpret_cast<LPARAM>(&state)); return state.found;
}
int property(HWND hwnd, const wchar_t* name) { return static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(hwnd, name))); }
std::string utf8(const std::wstring& value) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n, nullptr, nullptr);
    return result;
}
BOOL CALLBACK describe_child(HWND hwnd, LPARAM) {
    wchar_t cls[100]{}, title[1000]{}; GetClassNameW(hwnd, cls, 100); GetWindowTextW(hwnd, title, 1000);
    if (std::wstring(cls) == L"Static" && title[0]) std::cerr << "  " << utf8(title) << "\n";
    return TRUE;
}
BOOL CALLBACK describe_owned(HWND hwnd, LPARAM lp) {
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    if (pid != static_cast<DWORD>(lp) || !IsWindowVisible(hwnd)) return TRUE;
    wchar_t cls[100]{}, title[1000]{}; GetClassNameW(hwnd, cls, 100); GetWindowTextW(hwnd, title, 1000);
    std::cerr << "  window=" << utf8(cls) << " title=" << utf8(title) << "\n";
    EnumChildWindows(hwnd, describe_child, 0); return TRUE;
}
void capture_failure(HWND hwnd) {
    if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return;
    RECT rc{}; GetClientRect(hwnd, &rc); POINT origin{0, 0}; ClientToScreen(hwnd, &origin);
    const int width = rc.right, height = rc.bottom;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    auto old = SelectObject(memory, bitmap);
    const bool copied = BitBlt(memory, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY) != FALSE;
    SelectObject(memory, old);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    const bool ok = copied && GetDIBits(memory, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS) == height;
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
    if (!ok) return;
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    const auto path = std::filesystem::temp_directory_path() / L"colortile_screen_failure.bmp";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    if (out.good()) std::cerr << "  failure_capture=" << utf8(path.wstring()) << "\n";
}
bool message(HWND hwnd, UINT msg, WPARAM wp = 0, LPARAM lp = 0, DWORD_PTR* reply = nullptr) {
    DWORD_PTR result = 0;
    const bool ok = SendMessageTimeoutW(hwnd, msg, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result) != 0;
    if (reply) *reply = result; return ok;
}
int route_count(HWND hwnd) {
    DWORD_PTR value = 0;
    return message(GetDlgItem(hwnd, kRoute), LB_GETCOUNT, 0, 0, &value) ? static_cast<int>(value) : -1;
}
struct Process {
    PROCESS_INFORMATION info{};
    HWND hwnd = nullptr;
    ~Process() {
        if (hwnd && IsWindow(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        if (info.hProcess) {
            if (WaitForSingleObject(info.hProcess, 3000) != WAIT_OBJECT_0) TerminateProcess(info.hProcess, 3);
            CloseHandle(info.hProcess);
        }
        if (info.hThread) CloseHandle(info.hThread);
    }
    bool launch(const std::wstring& exe, const std::wstring& args, const wchar_t* className) {
        std::wstring command = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) return false;
        return wait_for([&] { hwnd = find(info.dwProcessId, className); return hwnd && IsWindowVisible(hwnd); }, 10000);
    }
};
bool mouse_move(POINT point) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (point.x < vx || point.y < vy || point.x >= vx + vw || point.y >= vy + vh) return false;
    INPUT input{}; input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>((static_cast<long long>(point.x - vx) * 65536 + 32768) / vw);
    input.mi.dy = static_cast<LONG>((static_cast<long long>(point.y - vy) * 65536 + 32768) / vh);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}
bool mouse_button(DWORD flags) {
    INPUT input{}; input.type = INPUT_MOUSE; input.mi.dwFlags = flags;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}
bool press_escape(HWND foreground) {
    if (GetAncestor(GetForegroundWindow(), GA_ROOT) != foreground) return false;
    INPUT key[2]{}; key[0].type = key[1].type = INPUT_KEYBOARD;
    key[0].ki.wVk = key[1].ki.wVk = VK_ESCAPE; key[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, key, sizeof(INPUT)) == 2;
}
struct Scenario {
    Process mock, app;
    RECT board{};
    std::string error;
    bool refresh_board_rect() {
        const int cell = property(mock.hwnd, L"ColorTileMockCellSize");
        POINT origin{property(mock.hwnd, L"ColorTileMockBoardLeft") - 1, property(mock.hwnd, L"ColorTileMockBoardTop") - 1};
        if (cell < 6 || origin.x < 0 || origin.y < 0 || !ClientToScreen(mock.hwnd, &origin)) return false;
        board = {origin.x - 2, origin.y - 2, origin.x + 23 * cell + 2, origin.y + 15 * cell + 2};
        return true;
    }
    bool prepare(const std::wstring& appPath, const std::wstring& mockPath, const std::wstring& samplePath) {
        if (!mock.launch(mockPath, L"--mock \"" + samplePath + L"\"", kMockClass)) { error = "mock did not launch"; return false; }
        if (!wait_for([&] { return property(mock.hwnd, kRemaining) > 0 && property(mock.hwnd, L"ColorTileMockReady") == 1; })) { error = "mock not ready"; return false; }
        if (!refresh_board_rect()) { error = "mock board geometry unavailable"; return false; }
        if (!app.launch(appPath, L"", kAppClass)) { error = "GUI did not launch"; return false; }
        if (!wait_for([&] { return IsWindowEnabled(GetDlgItem(app.hwnd, kScreenAuto)) != FALSE; })) { error = "GUI controls not ready"; return false; }
        if (!message(GetDlgItem(app.hwnd, kMode), CB_SETCURSEL, 0) ||
            !message(GetDlgItem(app.hwnd, kLimit), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"0.5")) ||
            !message(GetDlgItem(app.hwnd, kInterval), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"0.1"))) {
            error = "could not set search budget and playback interval"; return false;
        }
        return true;
    }
    bool select(int commandId) {
        ShowWindow(app.hwnd, SW_RESTORE); SetForegroundWindow(app.hwnd);
        if (!wait_for([&] { return GetForegroundWindow() == app.hwnd; })) { error = "GUI could not receive focus"; return false; }
        if (!PostMessageW(app.hwnd, WM_COMMAND, MAKEWPARAM(commandId, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(app.hwnd, commandId)))) {
            error = "could not request screen selection"; return false;
        }
        HWND selector = nullptr;
        if (!wait_for([&] { selector = find(app.info.dwProcessId, kSelectorClass); return selector && IsWindowVisible(selector) && GetForegroundWindow() == selector; })) {
            error = "screen selector did not appear"; return false;
        }
        // A window manager may complete an asynchronous initial placement after
        // the mock reports ready. Use the current physical board coordinates.
        if (!refresh_board_rect()) { error = "mock board geometry unavailable at selection"; return false; }
        POINT selectorOrigin{0, 0}; ClientToScreen(selector, &selectorOrigin);
        const int virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN), virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        // Select what is shown in the frozen desktop image. Its pixel origin is
        // the selector's client origin, which an external Wine window manager
        // can relocate even though the app requested the virtual-screen origin.
        const POINT start{selectorOrigin.x + board.left - virtualX, selectorOrigin.y + board.top - virtualY};
        const POINT finish{selectorOrigin.x + board.right - virtualX, selectorOrigin.y + board.bottom - virtualY};
        std::cout << "INFO: selector_origin=" << selectorOrigin.x << ',' << selectorOrigin.y
                  << " board=" << board.left << ',' << board.top << ',' << board.right << ',' << board.bottom << "\n" << std::flush;
        if (GetAncestor(WindowFromPoint(start), GA_ROOT) != selector || GetAncestor(WindowFromPoint(finish), GA_ROOT) != selector) {
            error = "private selector does not cover intended drag"; return false;
        }
        if (!mouse_move(start)) { error = "selection start move failed"; return false; }
        if (!wait_for([&] { POINT point{}; return GetCursorPos(&point) && point.x == start.x && point.y == start.y; }, 1000)) {
            error = "selection cursor did not reach start point"; return false;
        }
        if (!mouse_button(MOUSEEVENTF_LEFTDOWN)) { error = "selection mouse down failed"; return false; }
        const DWORD selectorThread = GetWindowThreadProcessId(selector, nullptr);
        if (!wait_for([&] { GUITHREADINFO state{}; state.cbSize = sizeof(state); return GetGUIThreadInfo(selectorThread, &state) && state.hwndCapture == selector; }, 3000)) {
            mouse_button(MOUSEEVENTF_LEFTUP);
            error = "selector did not acknowledge mouse down / capture"; return false;
        }
        const bool moved = mouse_move(finish) && wait_for([&] {
            POINT point{}; return GetCursorPos(&point) && point.x == finish.x && point.y == finish.y;
        }, 1000);
        const bool released = mouse_button(MOUSEEVENTF_LEFTUP);
        if (!moved || !released) { error = "selection drag failed"; return false; }
        if (!wait_for([&] {
                return IsIconic(app.hwnd) && route_count(app.hwnd) > 0 && GetForegroundWindow() == mock.hwnd &&
                       IsWindowEnabled(GetDlgItem(app.hwnd, kSearchStop)) == FALSE &&
                       IsWindowEnabled(GetDlgItem(app.hwnd, kLiveStop)) != FALSE &&
                       IsWindowEnabled(GetDlgItem(app.hwnd, kScreenAuto)) == FALSE;
            }, 12000)) {
            std::cerr << "  iconic=" << IsIconic(app.hwnd) << " routes=" << route_count(app.hwnd)
                      << " clicks=" << property(mock.hwnd, kClicks) << " live_stop_enabled=" << IsWindowEnabled(GetDlgItem(app.hwnd, kLiveStop))
                      << " selector_alive=" << IsWindow(selector) << "\n";
            EnumWindows(describe_owned, static_cast<LPARAM>(app.info.dwProcessId));
            capture_failure(app.hwnd);
            error = "GUI did not finish recognition/search and enter live mode"; return false;
        }
        return true;
    }
    bool stopped() const {
        return IsWindowEnabled(GetDlgItem(app.hwnd, kLiveStop)) == FALSE &&
               IsWindowEnabled(GetDlgItem(app.hwnd, kScreenAuto)) != FALSE &&
               IsWindowEnabled(GetDlgItem(app.hwnd, kSearchStop)) == FALSE;
    }
    bool wait_stopped(DWORD timeout = 5000) {
        ULONGLONG since = 0;
        return wait_for([&] {
            const ULONGLONG now = GetTickCount64();
            if (!stopped()) { since = 0; return false; }
            if (!since) since = now;
            return now - since >= 150;
        }, timeout);
    }
    bool no_more_clicks(DWORD duration = 600) {
        const int count = property(mock.hwnd, kClicks); settle(duration);
        return property(mock.hwnd, kClicks) == count && property(mock.hwnd, kOutside) == 0;
    }
    void diagnose() {
        wchar_t foregroundClass[128]{}; GetClassNameW(GetForegroundWindow(), foregroundClass, 128);
        std::cerr << "  remaining=" << property(mock.hwnd, kRemaining) << " clicks=" << property(mock.hwnd, kClicks)
                  << " route=" << route_count(app.hwnd) << " outside=" << property(mock.hwnd, kOutside)
                  << " foreground=" << utf8(foregroundClass) << " iconic=" << IsIconic(app.hwnd)
                  << " stopped=" << stopped() << "\n";
        capture_failure(app.hwnd);
    }
};
void dpi_awareness() {
    using Dpi = BOOL(WINAPI*)(HANDLE);
    const auto dpi = reinterpret_cast<Dpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (dpi) dpi(reinterpret_cast<HANDLE>(-4)); else SetProcessDPIAware();
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    dpi_awareness();
    if (argc != 4) { std::cerr << "Usage: test_screen_gui_windows.exe GUI_EXE MOCK_TEST_EXE SAMPLE_CTB\n"; return 2; }
    const auto appPath = std::filesystem::absolute(argv[1]).wstring();
    const auto mockPath = std::filesystem::absolute(argv[2]).wstring();
    const auto samplePath = std::filesystem::absolute(argv[3]).wstring();
    auto fail = [](const char* stage, const std::string& reason) { std::cerr << "FAIL: " << stage << ": " << reason << "\n"; return 1; };
    {
        Scenario s;
        if (!s.prepare(appPath, mockPath, samplePath) || !s.select(kScreenAuto)) return fail("automatic start", s.error);
        if (!s.wait_stopped(35000)) {
            s.diagnose();
            return fail("automatic completion", "live mode did not finish with a cleared board");
        }
        const int clicks = property(s.mock.hwnd, kClicks), count = route_count(s.app.hwnd);
        if (property(s.mock.hwnd, kRemaining) != 0 || clicks <= 0 || clicks != count || property(s.mock.hwnd, kOutside) != 0) {
            std::cerr << "  remaining=" << property(s.mock.hwnd, kRemaining) << " clicks=" << clicks << " route=" << count
                      << " outside=" << property(s.mock.hwnd, kOutside) << "\n";
            capture_failure(s.app.hwnd);
            return fail("automatic completion", "board did not clear with exactly one click per route entry");
        }
        if (!s.no_more_clicks(200)) return fail("automatic completion", "clicks continued after completion");
        std::cout << "PASS: GUI region selection, recognition, search and full automatic clear; clicks=" << clicks << "\n" << std::flush;
    }
    {
        Scenario s;
        if (!s.prepare(appPath, mockPath, samplePath) || !s.select(kScreenHint)) return fail("hint start", s.error);
        HWND hint = nullptr;
        if (!wait_for([&] { hint = find(s.app.info.dwProcessId, kHintClass); return hint && IsWindowVisible(hint); }))
            return fail("hint display", "hint ring did not appear");
        if (!s.no_more_clicks(600) || property(s.mock.hwnd, kClicks) != 0) return fail("hint display", "hint mode injected a click");
        RECT ring{}; GetWindowRect(hint, &ring);
        const POINT center{(ring.left + ring.right) / 2, (ring.top + ring.bottom) / 2};
        if (GetForegroundWindow() != s.mock.hwnd || GetAncestor(WindowFromPoint(center), GA_ROOT) != s.mock.hwnd ||
            !PtInRect(&s.board, center)) return fail("hint interaction", "ring is not safely click-through to mock board");
        if (!mouse_move(center) || !mouse_button(MOUSEEVENTF_LEFTDOWN) || !mouse_button(MOUSEEVENTF_LEFTUP))
            return fail("hint interaction", "private manual-style click could not be sent");
        if (!wait_for([&] {
                wchar_t title[100]{}; GetWindowTextW(hint, title, 100);
                return property(s.mock.hwnd, kClicks) == 1 && std::wstring(title).find(L" 2 / ") != std::wstring::npos && IsWindowVisible(hint);
            })) return fail("hint interaction", "hint did not advance after one user click");
        if (!s.no_more_clicks(300)) return fail("hint interaction", "hint mode injected an additional click");
        if (!press_escape(s.mock.hwnd) || !s.wait_stopped()) return fail("hint Escape stop", "Escape did not stop mode");
        hint = find(s.app.info.dwProcessId, kHintClass);
        if ((hint && IsWindowVisible(hint)) || !s.no_more_clicks()) return fail("hint Escape stop", "hint or clicks remained after stop");
        std::cout << "PASS: hint mode injected no clicks, passed one user click through, advanced hint and stopped on Escape\n" << std::flush;
    }
    {
        Scenario s;
        if (!s.prepare(appPath, mockPath, samplePath) || !s.select(kScreenAuto)) return fail("automatic Escape setup", s.error);
        if (!wait_for([&] { return property(s.mock.hwnd, kClicks) >= 2; })) return fail("automatic Escape setup", "automatic clicks did not begin");
        if (!press_escape(s.mock.hwnd) || !s.wait_stopped()) return fail("automatic Escape stop", "Escape did not stop mode");
        if (property(s.mock.hwnd, kRemaining) == 0 || !s.no_more_clicks()) return fail("automatic Escape stop", "did not stop partway without further clicks");
        std::cout << "PASS: automatic mode stopped partway on Escape with no subsequent click\n" << std::flush;
    }
    {
        Scenario s;
        if (!s.prepare(appPath, mockPath, samplePath) || !s.select(kScreenAuto)) return fail("foreground guard setup", s.error);
        if (!wait_for([&] { return property(s.mock.hwnd, kClicks) >= 2; })) return fail("foreground guard setup", "automatic clicks did not begin");
        ShowWindow(s.app.hwnd, SW_RESTORE); SetForegroundWindow(s.app.hwnd);
        if (!s.wait_stopped()) return fail("foreground guard", "bringing GUI foreground did not stop live mode");
        if (property(s.mock.hwnd, kRemaining) == 0 || !s.no_more_clicks()) return fail("foreground guard", "clicks continued after foreground switch");
        std::cout << "PASS: foreground switch stopped automatic mode with no subsequent click\n" << std::flush;
    }
    return 0;
}
