// Windows GUI regression: menus and minimum-size layout, adjustable playback,
// cancel with retained results, then restart and close while the worker runs.
// All UI messages target the process started by this test; no global input.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>

namespace {
constexpr int kLimit = 107, kStart = 108, kStop = 109, kReset = 110;
constexpr int kPrevious = 111, kNext = 112, kPlay = 113, kRoute = 114, kMode = 118;
constexpr int kInterval = 119, kSpin = 120, kScreenAuto = 121, kScreenHint = 122, kLiveStop = 123;
struct FindWindowState { DWORD pid; HWND hwnd = nullptr; };
BOOL CALLBACK match_window(HWND hwnd, LPARAM context) {
    auto& state = *reinterpret_cast<FindWindowState*>(context);
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    wchar_t className[128]{}; GetClassNameW(hwnd, className, 128);
    if (pid == state.pid && std::wstring(className) == L"ColorTileSolverWindow") {
        state.hwnd = hwnd; return FALSE;
    }
    return TRUE;
}
bool wait_for(const std::function<bool()>& condition, DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    do {
        if (condition()) return true;
        Sleep(20);
    } while (GetTickCount64() < deadline);
    return condition();
}
bool message(HWND hwnd, UINT msg, WPARAM wp = 0, LPARAM lp = 0, DWORD_PTR* reply = nullptr) {
    DWORD_PTR result = 0;
    const bool ok = SendMessageTimeoutW(hwnd, msg, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                        2000, &result) != 0;
    if (reply) *reply = result;
    return ok;
}
bool command(HWND hwnd, int id) {
    return message(hwnd, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
                   reinterpret_cast<LPARAM>(GetDlgItem(hwnd, id)));
}
bool edit_interval(HWND hwnd, const wchar_t* seconds) {
    HWND edit = GetDlgItem(hwnd, kInterval);
    return message(edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(seconds)) &&
           message(hwnd, WM_COMMAND, MAKEWPARAM(kInterval, EN_KILLFOCUS), reinterpret_cast<LPARAM>(edit));
}
double interval_value(HWND hwnd) {
    wchar_t value[64]{};
    if (!message(GetDlgItem(hwnd, kInterval), WM_GETTEXT, 64, reinterpret_cast<LPARAM>(value))) return -1;
    wchar_t* end = nullptr; const double result = wcstod(value, &end);
    return end != value && end && !*end && std::isfinite(result) ? result : -1;
}
bool spin_once(HWND hwnd, bool up) {
    HWND spin = GetDlgItem(hwnd, kSpin); RECT r{}; GetClientRect(spin, &r);
    const LPARAM location = MAKELPARAM(r.right / 2, up ? r.bottom / 4 : (r.bottom * 3) / 4);
    // Let the real control generate its in-process UDN_DELTAPOS notification.
    // WM_NOTIFY itself cannot safely be sent between processes.
    return message(spin, WM_LBUTTONDOWN, MK_LBUTTON, location) &&
           message(spin, WM_LBUTTONUP, 0, location);
}
int selected_route(HWND hwnd) {
    DWORD_PTR selected = 0;
    return message(GetDlgItem(hwnd, kRoute), LB_GETCURSEL, 0, 0, &selected) ? static_cast<int>(selected) : -1;
}
bool compact_layout(HWND hwnd) {
    RECT before{}; GetWindowRect(hwnd, &before);
    if (!SetWindowPos(hwnd, nullptr, before.left, before.top, 960, 720, SWP_NOZORDER | SWP_NOACTIVATE)) return false;
    if (!message(hwnd, WM_NULL)) return false; // Wait until the resize is processed.
    RECT client{}; GetClientRect(hwnd, &client);
    const std::array<int, 13> controls{{kMode, kLimit, kStart, kStop, kScreenAuto, kScreenHint,
                                      kLiveStop, kReset, kPrevious, kNext, kPlay, kInterval, kSpin}};
    std::array<RECT, controls.size()> bounds{};
    bool ok = true;
    for (size_t i = 0; i < controls.size(); ++i) {
        HWND child = GetDlgItem(hwnd, controls[i]);
        if (!child || !IsWindowVisible(child) || !GetWindowRect(child, &bounds[i])) { ok = false; break; }
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&bounds[i]), 2);
        if (bounds[i].left < 0 || bounds[i].top < 0 || bounds[i].right > client.right || bounds[i].bottom > client.bottom) {
            ok = false; break;
        }
        for (size_t j = 0; j < i; ++j) {
            RECT overlap{};
            if (IntersectRect(&overlap, &bounds[i], &bounds[j])) { ok = false; break; }
        }
    }
    SetWindowPos(hwnd, nullptr, before.left, before.top, before.right - before.left,
                 before.bottom - before.top, SWP_NOZORDER | SWP_NOACTIVATE);
    message(hwnd, WM_NULL);
    return ok;
}
bool has_menu_command(HMENU menu, UINT id) {
    SetLastError(0);
    const int count = GetMenuItemCount(menu);
    if (count < 0) std::cerr << "menu diagnostic: handle=" << menu << " GetMenuItemCount error=" << GetLastError() << " command=" << id << "\n";
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW item{}; item.cbSize = sizeof(item); item.fMask = MIIM_ID | MIIM_SUBMENU;
        SetLastError(0);
        const BOOL read = GetMenuItemInfoW(menu, i, TRUE, &item);
        if (!read) std::cerr << "menu diagnostic: handle=" << menu << " index=" << i << " GetMenuItemInfo error=" << GetLastError() << "\n";
        if (read &&
            (item.wID == id || (item.hSubMenu && has_menu_command(item.hSubMenu, id)))) return true;
    }
    return false;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::cerr << "Usage: test_gui_windows.exe APP_EXE SAMPLE_PNG\n";
        return 2;
    }
    std::wstring app = argv[1], image = argv[2];
    std::wstring cmd = L"\"" + app + L"\" \"" + image + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(app.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        std::cerr << "FAIL: CreateProcess error=" << GetLastError() << "\n"; return 1;
    }
    CloseHandle(process.hThread);
    HWND hwnd = nullptr;
    auto fail = [&](const char* reason) {
        std::cerr << "FAIL: " << reason << "\n";
        if (hwnd && IsWindow(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0)
            TerminateProcess(process.hProcess, 3);
        CloseHandle(process.hProcess); return 1;
    };
    if (!wait_for([&] {
            FindWindowState find{process.dwProcessId}; EnumWindows(match_window, reinterpret_cast<LPARAM>(&find));
            hwnd = find.hwnd;
            return hwnd && IsWindowEnabled(GetDlgItem(hwnd, kStart));
        }, 15000)) return fail("app did not load screenshot / enable Start");
    HMENU menu = GetMenu(hwnd);
    if (!menu) return fail("native menu bar is missing");
    const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    const bool canReadMenu = GetMenuItemCount(menu) >= 0;
    if (!canReadMenu && !wine) return fail("cannot inspect native menu items");
    if (!canReadMenu) std::cout << "SKIP: Wine cross-process menu-item metadata is unavailable; menu bar presence and toolbar removal are checked\n";
    for (int id : {101, 102, 103, 104, 105, 106, 116, 117}) {
        if (GetDlgItem(hwnd, id)) return fail("infrequent file / board action still occupies a toolbar button");
        if (canReadMenu && !has_menu_command(menu, id)) { std::cerr << "missing menu command id=" << id << "\n"; return fail("file / board command is missing from menus"); }
    }
    if (!compact_layout(hwnd)) return fail("controls overlap or leave the client area at minimum size");
    if (!edit_interval(hwnd, L"0.6") || !spin_once(hwnd, true) || std::abs(interval_value(hwnd) - 0.7) > 0.001)
        return fail("upper interval spinner did not increase by 0.1 seconds");
    if (!spin_once(hwnd, false) || std::abs(interval_value(hwnd) - 0.6) > 0.001)
        return fail("lower interval spinner did not decrease by 0.1 seconds");
    // Spin validation is intentionally nonmodal: malformed text must restore
    // the last valid value before the arrow applies its one-tenth step.
    if (!message(GetDlgItem(hwnd, kInterval), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"NaN")) ||
        !spin_once(hwnd, true) || std::abs(interval_value(hwnd) - 0.7) > 0.001)
        return fail("invalid interval corrupted the last valid setting");
    // Choose timed mode and a long enough budget to test an explicit cancel.
    if (!message(GetDlgItem(hwnd, kMode), CB_SETCURSEL, 0) ||
        !message(GetDlgItem(hwnd, kLimit), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"10")))
        return fail("could not set timed mode / budget");
    if (!command(hwnd, kStart) || !wait_for([&] { return IsWindowEnabled(GetDlgItem(hwnd, kStop)) != FALSE; }, 1000))
        return fail("first run did not enter busy state");
    Sleep(150);
    const ULONGLONG cancelStarted = GetTickCount64();
    // update_enabled changes distinct native controls sequentially. Observing
    // Start alone can catch the brief interval before Stop is disabled.
    if (!command(hwnd, kStop) || !wait_for([&] {
            return IsWindowEnabled(GetDlgItem(hwnd, kStart)) != FALSE &&
                   IsWindowEnabled(GetDlgItem(hwnd, kStop)) == FALSE;
        }, 5000))
        return fail("cancel did not restore ready state");
    const ULONGLONG cancelMs = GetTickCount64() - cancelStarted;
    if (IsWindowEnabled(GetDlgItem(hwnd, kStop))) return fail("Stop remained enabled after cancellation");
    DWORD_PTR routeCount = 0, selected = 0;
    if (!message(GetDlgItem(hwnd, kRoute), LB_GETCOUNT, 0, 0, &routeCount) ||
        static_cast<LRESULT>(routeCount) < 2)
        return fail("cancelled run did not retain a useful route");
    if (!command(hwnd, kNext) || !message(GetDlgItem(hwnd, kRoute), LB_GETCURSEL, 0, 0, &selected) || selected != 1)
        return fail("Next did not move to the second route entry");
    if (!command(hwnd, kReset) || !message(GetDlgItem(hwnd, kRoute), LB_GETCURSEL, 0, 0, &selected) || selected != 0)
        return fail("Reset did not return to the first route entry");
    if (routeCount < 8) return fail("retained route is too short to test adjustable playback");
    if (!edit_interval(hwnd, L"1.0") || !command(hwnd, kPlay)) return fail("slow playback did not start");
    Sleep(350);
    if (selected_route(hwnd) != 0) return fail("1.0-second playback advanced too early");
    if (!wait_for([&] { return selected_route(hwnd) >= 1; }, 2200)) return fail("1.0-second playback did not advance");
    const int slowPosition = selected_route(hwnd);
    const ULONGLONG fastStarted = GetTickCount64();
    if (!edit_interval(hwnd, L"0.1") || std::abs(interval_value(hwnd) - 0.1) > 0.001)
        return fail("could not edit the playback interval while playing");
    if (!wait_for([&] { return selected_route(hwnd) >= slowPosition + 5; }, 1800))
        return fail("live change to 0.1-second playback did not accelerate the existing timer");
    const ULONGLONG fastFiveStepsMs = GetTickCount64() - fastStarted;
    if (!command(hwnd, kPlay)) return fail("could not stop playback");
    const int stoppedPosition = selected_route(hwnd);
    Sleep(250);
    if (selected_route(hwnd) != stoppedPosition) return fail("playback advanced after Stop");
    if (!command(hwnd, kReset)) return fail("could not reset after playback test");
    if (!command(hwnd, kStart) || !wait_for([&] { return IsWindowEnabled(GetDlgItem(hwnd, kStop)) != FALSE; }, 1000))
        return fail("second run did not enter busy state");
    const ULONGLONG closeStarted = GetTickCount64();
    if (!PostMessageW(hwnd, WM_CLOSE, 0, 0) || WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0)
        return fail("closing while busy did not exit promptly");
    const ULONGLONG closeMs = GetTickCount64() - closeStarted;
    DWORD exitCode = 0; GetExitCodeProcess(process.hProcess, &exitCode); CloseHandle(process.hProcess);
    if (exitCode != 0) { std::cerr << "FAIL: app exit code=" << exitCode << "\n"; return 1; }
    std::cout << "PASS: menu layout; 0.1-second spinner; invalid value retention; live playback speed change; "
              << "cancel retained " << routeCount << " steps; Next/Reset; busy restart and close\n"
              << "fast_five_steps_ms=" << fastFiveStepsMs << "\ncancel_response_ms=" << cancelMs
              << "\nclose_response_ms=" << closeMs << "\n";
    return 0;
}
