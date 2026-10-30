// Desktop integration test. Every injected click is restricted to a mock game
// process created by this executable. Run in a disposable, isolated desktop.
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
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include "core.h"
#include "desktop.h"

namespace {
constexpr wchar_t kMockClass[] = L"ColorTileSolverMockGame";
constexpr wchar_t kCoverClass[] = L"ColorTileSolverMockCover";
constexpr int kCell = 24, kMargin = 24;
constexpr UINT kResetMock = WM_APP + 42;
constexpr UINT kChangeMock = WM_APP + 43;
const wchar_t* const kClicks = L"ColorTileMockClickCount";
const wchar_t* const kLastCell = L"ColorTileMockLastCell";
const wchar_t* const kOutside = L"ColorTileMockOutsideClicks";
const wchar_t* const kRemaining = L"ColorTileMockRemaining";

void set_property(HWND hwnd, const wchar_t* name, int n) {
    SetPropW(hwnd, name, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(n)));
}
int property(HWND hwnd, const wchar_t* name) {
    return static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(hwnd, name)));
}
int remaining(const ct::Board& board) {
    return static_cast<int>(std::count_if(board.cells.begin(), board.cells.end(), [](uint8_t x) { return x != 0; }));
}
bool read_board(const std::wstring& path, ct::Board& board) {
    std::ifstream file{std::filesystem::path(path)};
    if (!(file >> board.width >> board.height) || board.width < 1 || board.width > 32 || board.height < 1 || board.height > 32) return false;
    board.cells.resize(static_cast<size_t>(board.width) * board.height);
    for (auto& c : board.cells) { int n; if (!(file >> n) || n < 0 || n > 10) return false; c = static_cast<uint8_t>(n); }
    return ct::validate_board(board);
}
struct MockState {
    ct::Board initial, board;
    int clicks = 0, outside = 0;
};
void draw_mock(HWND hwnd, const ct::Board& board) {
    PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
    RECT client{}; GetClientRect(hwnd, &client);
    HBRUSH empty = CreateSolidBrush(RGB(244, 246, 248)); FillRect(dc, &client, empty); DeleteObject(empty);
    for (int r = 0; r < board.height; ++r) for (int c = 0; c < board.width; ++c) {
        const int color = board.cells[r * board.width + c]; if (!color) continue;
        const uint32_t rgb = ct::kPalette[color];
        HBRUSH brush = CreateSolidBrush(RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255));
        RECT tile{kMargin + c * kCell + 2, kMargin + r * kCell + 2,
                  kMargin + (c + 1) * kCell - 2, kMargin + (r + 1) * kCell - 2};
        FillRect(dc, &tile, brush); DeleteObject(brush);
    }
    EndPaint(hwnd, &paint);
}
LRESULT CALLBACK mock_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<MockState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        state = static_cast<MockState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE:
        set_property(hwnd, kRemaining, remaining(state->board));
        // Offsets are plus one so that a zero coordinate is distinguishable
        // from a missing property when an external test driver reads them.
        set_property(hwnd, L"ColorTileMockBoardLeft", kMargin + 1);
        set_property(hwnd, L"ColorTileMockBoardTop", kMargin + 1);
        set_property(hwnd, L"ColorTileMockCellSize", kCell);
        return 0;
    case WM_PAINT: draw_mock(hwnd, state->board); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp) - kMargin, y = GET_Y_LPARAM(lp) - kMargin;
        if (x < 0 || y < 0 || x >= state->board.width * kCell || y >= state->board.height * kCell) {
            set_property(hwnd, kOutside, ++state->outside); return 0;
        }
        const int cell = (y / kCell) * state->board.width + x / kCell;
        set_property(hwnd, kClicks, ++state->clicks); set_property(hwnd, kLastCell, cell + 1);
        ct::apply_move(state->board, cell);
        set_property(hwnd, kRemaining, remaining(state->board));
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); return 0;
    }
    case kResetMock:
        state->board = state->initial; state->clicks = state->outside = 0;
        set_property(hwnd, kClicks, 0); set_property(hwnd, kLastCell, 0); set_property(hwnd, kOutside, 0);
        set_property(hwnd, kRemaining, remaining(state->board));
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); return 0;
    case kChangeMock:
        if (wp < state->board.cells.size() && lp >= 0 && lp <= 10) state->board.cells[wp] = static_cast<uint8_t>(lp);
        set_property(hwnd, kRemaining, remaining(state->board));
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void pump() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}
bool wait_for(const std::function<bool()>& predicate, DWORD timeout = 3000) {
    const ULONGLONG until = GetTickCount64() + timeout;
    do { pump(); if (predicate()) return true; Sleep(10); } while (GetTickCount64() < until);
    pump(); return predicate();
}
struct WindowQuery { DWORD pid = 0; HWND result = nullptr; };
BOOL CALLBACK find_mock(HWND hwnd, LPARAM lp) {
    auto& q = *reinterpret_cast<WindowQuery*>(lp);
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    wchar_t name[100]{}; GetClassNameW(hwnd, name, 100);
    if (pid == q.pid && std::wstring(name) == kMockClass) { q.result = hwnd; return FALSE; }
    return TRUE;
}
struct Child {
    PROCESS_INFORMATION process{};
    HWND hwnd = nullptr;
    ~Child() {
        if (hwnd && IsWindow(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 3000) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 3);
            CloseHandle(process.hProcess);
        }
        if (process.hThread) CloseHandle(process.hThread);
    }
};
struct Cover {
    HWND hwnd = nullptr;
    ~Cover() { if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd); }
};
void dpi_awareness() {
    using Dpi = BOOL(WINAPI*)(HANDLE);
    auto dpi = reinterpret_cast<Dpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (dpi) dpi(reinterpret_cast<HANDLE>(-4)); else SetProcessDPIAware();
}
int mock_main(const std::wstring& boardPath) {
    MockState state;
    if (!read_board(boardPath, state.initial)) return 2;
    state.board = state.initial;
    WNDCLASSW wc{}; wc.lpfnWndProc = mock_proc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kMockClass; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    RECT rc{0, 0, state.board.width * kCell + 2 * kMargin, state.board.height * kCell + 2 * kMargin};
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
    HWND hwnd = CreateWindowExW(0, kMockClass, L"ColorTile Solver test board", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                80, 70, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, &state);
    if (!hwnd) return 2;
    UpdateWindow(hwnd); SetForegroundWindow(hwnd);
    set_property(hwnd, L"ColorTileMockReady", 1);
    MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
POINT cell_center(const desktop::Target& target, const ct::Board& board, int cell) {
    return {target.selection.left + (cell % board.width) * kCell + kCell / 2,
            target.selection.top + (cell / board.width) * kCell + kCell / 2};
}
RECT cell_rect(const desktop::Target& target, const ct::Board& board, int cell) {
    const POINT p = cell_center(target, board, cell);
    return {p.x - kCell / 2, p.y - kCell / 2, p.x + kCell / 2, p.y + kCell / 2};
}
bool captured_matches(const desktop::Target& target, const ct::Board& board, std::wstring& error) {
    ct::Image image;
    if (!desktop::capture(target, image, error)) return false;
    ct::GridGeometry grid; grid.left = grid.top = 0; grid.cell_width = grid.cell_height = kCell;
    grid.columns = board.width; grid.rows = board.height;
    auto found = ct::sample_grid(image, grid);
    if (!found.ok || found.board.cells != board.cells) {
        error = L"captured board differs from mock board";
        if (found.ok) {
            for (size_t i = 0; i < board.cells.size(); ++i) if (found.board.cells[i] != board.cells[i]) {
                const size_t pixel = (static_cast<size_t>(i / board.width * kCell + kCell / 2) * image.width + i % board.width * kCell + kCell / 2) * 3;
                error += L": cell=" + std::to_wstring(i) + L" expected=" + std::to_wstring(board.cells[i]) +
                    L" found=" + std::to_wstring(found.board.cells[i]) + L" rgb=" + std::to_wstring(image.rgb[pixel]) + L"," +
                    std::to_wstring(image.rgb[pixel + 1]) + L"," + std::to_wstring(image.rgb[pixel + 2]);
                break;
            }
        }
        return false;
    }
    return true;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    dpi_awareness();
    if (argc == 3 && std::wstring(argv[1]) == L"--mock") return mock_main(argv[2]);
    if (argc != 2) { std::cerr << "Usage: test_desktop_windows.exe SAMPLE_CTB\n"; return 2; }
    ct::Board board;
    if (!read_board(argv[1], board)) { std::cerr << "FAIL: invalid test board\n"; return 2; }
    Child child;
    std::wstring exe(32768, L'\0'); const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (!n || n >= exe.size()) return 2; exe.resize(n);
    const auto boardPath = std::filesystem::absolute(argv[1]).wstring();
    std::wstring command = L"\"" + exe + L"\" --mock \"" + boardPath + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child.process)) {
        std::cerr << "FAIL: could not create private mock game\n"; return 1;
    }
    auto fail = [&](const char* reason, const std::wstring& error = {}) {
        desktop::destroy_hint();
        std::cerr << "FAIL: " << reason << "\n";
        if (!error.empty()) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, error.data(), static_cast<int>(error.size()), nullptr, 0, nullptr, nullptr);
            std::string detail(bytes, '\0');
            WideCharToMultiByte(CP_UTF8, 0, error.data(), static_cast<int>(error.size()), detail.data(), bytes, nullptr, nullptr);
            std::cerr << detail << "\n";
        }
        return 1;
    };
    if (!wait_for([&] { WindowQuery q{child.process.dwProcessId}; EnumWindows(find_mock, reinterpret_cast<LPARAM>(&q)); child.hwnd = q.result; return child.hwnd && IsWindowVisible(child.hwnd) && property(child.hwnd, L"ColorTileMockReady") == 1; }, 10000))
        return fail("mock game did not appear");
    desktop::Target target;
    target.window = child.hwnd; target.process_id = child.process.dwProcessId; GetWindowRect(child.hwnd, &target.window_rect);
    POINT topLeft{kMargin, kMargin}; ClientToScreen(child.hwnd, &topLeft);
    target.selection = {topLeft.x, topLeft.y, topLeft.x + board.width * kCell, topLeft.y + board.height * kCell};
    std::wstring error;
    if (!desktop::focus_target(target, error)) return fail("focus_target", error);
    if (!wait_for([&] { return GetForegroundWindow() == child.hwnd; })) return fail("mock target did not receive foreground");
    if (!wait_for([&] { return captured_matches(target, board, error); })) return fail("initial capture / physical geometry", error);

    auto moves = ct::legal_moves(board);
    if (moves.size() < 2) return fail("test board needs multiple legal moves");
    const ct::Move first = moves.front();
    if (!desktop::show_hint(target, cell_rect(target, board, first.cell), 1, 2)) return fail("hint creation");
    pump();
    if (GetForegroundWindow() != child.hwnd) return fail("hint stole foreground");
    const POINT firstPoint = cell_center(target, board, first.cell);
    if (GetAncestor(WindowFromPoint(firstPoint), GA_ROOT) != child.hwnd) return fail("hint center is not click-through");
    if (!captured_matches(target, board, error)) return fail("hint contaminated captured pixels", error);
    if (!desktop::click_cell(target, firstPoint, error)) return fail("checked click failed", error);
    if (!wait_for([&] { return property(child.hwnd, kClicks) == 1; })) return fail("mock did not receive click through hint");
    if (property(child.hwnd, kLastCell) != first.cell + 1 || property(child.hwnd, kOutside) != 0) return fail("click reached wrong cell or outside board");
    ct::apply_move(board, first.cell);
    if (!captured_matches(target, board, error)) return fail("post-click capture differs from expected game state", error);

    // Coordinates outside the selected board must be rejected before injecting.
    if (desktop::click_cell(target, {target.selection.right, target.selection.top}, error)) return fail("outside selection was accepted");
    if (property(child.hwnd, kClicks) != 1 || property(child.hwnd, kOutside) != 0) return fail("outside rejection still delivered a click");
    desktop::hide_hint();

    // Moving the target invalidates the original physical coordinate binding.
    const RECT saved = target.window_rect;
    SetWindowPos(child.hwnd, nullptr, saved.left + 13, saved.top + 7, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (desktop::validate(target, error, false) || desktop::click_cell(target, firstPoint, error)) return fail("moved target was accepted");
    SetWindowPos(child.hwnd, nullptr, saved.left, saved.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (!desktop::focus_target(target, error)) return fail("could not restore mock target", error);
    if (!captured_matches(target, board, error)) return fail("restored target capture", error);

    // Put a private opaque test window above the central board cells. Foreground
    // and coverage checks are tested separately (require_foreground=false).
    WNDCLASSW coverClass{}; coverClass.lpfnWndProc = DefWindowProcW; coverClass.hInstance = GetModuleHandleW(nullptr);
    coverClass.lpszClassName = kCoverClass; coverClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassW(&coverClass);
    Cover cover;
    const int cx = (target.selection.left + target.selection.right) / 2, cy = (target.selection.top + target.selection.bottom) / 2;
    cover.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kCoverClass, L"Private test cover", WS_POPUP | WS_VISIBLE,
                                  cx - 40, cy - 40, 80, 80, nullptr, nullptr, coverClass.hInstance, nullptr);
    if (!cover.hwnd) return fail("could not create private occlusion window");
    UpdateWindow(cover.hwnd); SetForegroundWindow(cover.hwnd); pump();
    if (desktop::validate(target, error, false)) return fail("occluded target accepted without foreground requirement");
    ct::Image blocked;
    if (desktop::capture(target, blocked, error, false)) return fail("occluded target captured as playable");
    if (desktop::click_cell(target, firstPoint, error)) return fail("occluded / wrong foreground target clicked");
    ShowWindow(cover.hwnd, SW_HIDE);
    // Retain a different foreground window without covering the board.
    SetWindowPos(cover.hwnd, HWND_TOPMOST, saved.right + 10, saved.top, 20, 20, SWP_SHOWWINDOW);
    SetForegroundWindow(cover.hwnd); pump();
    if (GetForegroundWindow() == child.hwnd) return fail("test could not establish wrong foreground");
    if (desktop::validate(target, error) || desktop::click_cell(target, firstPoint, error)) return fail("wrong foreground target accepted");
    ShowWindow(cover.hwnd, SW_HIDE);
    if (property(child.hwnd, kClicks) != 1 || property(child.hwnd, kOutside) != 0) return fail("rejected operations still delivered clicks");
    if (!desktop::focus_target(target, error)) return fail("could not refocus target after guard tests", error);
    if (!captured_matches(target, board, error)) return fail("capture after guard tests", error);

    // Deliberately corrupting a recorded PID must fail even for an existing HWND.
    auto wrongProcess = target; wrongProcess.process_id = GetCurrentProcessId();
    if (desktop::validate(wrongProcess, error)) return fail("mismatched process was accepted");
    desktop::destroy_hint();
    PostMessageW(child.hwnd, WM_CLOSE, 0, 0);
    if (!wait_for([&] { return !IsWindow(child.hwnd); })) return fail("mock target did not close");
    if (desktop::validate(target, error) || desktop::click_cell(target, firstPoint, error)) return fail("destroyed target was accepted");
    std::cout << "PASS: private mock capture, click-through hint, exact injected cell, replayed board, selection bounds, moved window, occlusion, foreground, process identity and destroyed target guards\n";
    return 0;
}
