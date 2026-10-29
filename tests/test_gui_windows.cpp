// Windows GUI lifecycle regression: cancel with retained results, navigate,
// restart, then close while the worker is running. No global input injection.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <string>

namespace {
constexpr int kLimit = 107, kStart = 108, kStop = 109, kReset = 110;
constexpr int kNext = 112, kRoute = 114, kMode = 118;
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
    if (!command(hwnd, kStart) || !wait_for([&] { return IsWindowEnabled(GetDlgItem(hwnd, kStop)) != FALSE; }, 1000))
        return fail("second run did not enter busy state");
    const ULONGLONG closeStarted = GetTickCount64();
    if (!PostMessageW(hwnd, WM_CLOSE, 0, 0) || WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0)
        return fail("closing while busy did not exit promptly");
    const ULONGLONG closeMs = GetTickCount64() - closeStarted;
    DWORD exitCode = 0; GetExitCodeProcess(process.hProcess, &exitCode); CloseHandle(process.hProcess);
    if (exitCode != 0) { std::cerr << "FAIL: app exit code=" << exitCode << "\n"; return 1; }
    std::cout << "PASS: cancel retained " << routeCount << " steps; Next/Reset succeeded; busy restart and close succeeded\n"
              << "cancel_response_ms=" << cancelMs << "\nclose_response_ms=" << closeMs << "\n";
    return 0;
}
