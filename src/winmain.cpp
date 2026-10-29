#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "core.h"

namespace {
constexpr wchar_t kClassName[] = L"ColorTileSolverWindow";
constexpr wchar_t kCalibrationClass[] = L"ColorTileSolverCalibration";
constexpr UINT_PTR kUiTimer = 1, kPlayTimer = 2, kSmokeTimer = 3;
constexpr UINT kWorkerDone = WM_APP + 10;
constexpr COLORREF kBackground = RGB(242, 245, 250);
constexpr COLORREF kInk = RGB(27, 42, 65);
constexpr COLORREF kMuted = RGB(100, 115, 137);
constexpr COLORREF kAccent = RGB(28, 100, 220);
constexpr COLORREF kBorder = RGB(220, 227, 236);
constexpr int kPaletteStart = 200;
enum ControlId { Open = 101, Paste, Example, ImageView, Calibrate, Edit,
    Limit, Start, Stop, Reset, Previous, Next, Play, RouteList, Export,
    ImportBoard, SaveBoard, Mode };
const wchar_t* const kColorNames[] = {L"空白", L"灰", L"青", L"水色", L"緑", L"黄", L"茶", L"橙", L"赤", L"桃", L"紫"};

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L' ');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string utf8(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, ' ');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
COLORREF palette_color(int c) {
    uint32_t rgb = ct::kPalette[std::clamp(c, 0, 10)];
    return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}
int tile_count(const ct::Board& b) {
    return static_cast<int>(std::count_if(b.cells.begin(), b.cells.end(), [](uint8_t c) { return c != 0; }));
}
std::wstring count_string(uint64_t n) {
    std::wstring s = std::to_wstring(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, L",");
    return s;
}
std::wstring seconds_string(double seconds) {
    std::wostringstream out;
    out << std::fixed << std::setprecision(1) << seconds;
    return out.str();
}
std::wstring budget_string(double seconds) {
    std::wostringstream out; out << std::setprecision(12) << seconds; return out.str();
}
struct App {
    HWND hwnd = nullptr;
    HWND limit = nullptr, route = nullptr, mode = nullptr;
    HFONT font = nullptr, fontSmall = nullptr, fontTitle = nullptr, fontSection = nullptr, fontNumber = nullptr;
    double scale = 1.0;
    ct::Image image;
    std::vector<uint8_t> imageBgra;
    ct::Recognition recognition;
    ct::Board original;
    bool haveBoard = false;
    std::wstring imageName;
    std::vector<ct::Move> moves;
    int step = 0;
    bool showImage = false, editMode = false, playing = false;
    int selectedColor = 8;
    RECT boardRect{}, mainCard{}, sideCard{};
    double cellSize = 1;
    std::wstring headline = L"スクリーンショットを読み込んでください";
    std::wstring detail = L"画像を開く、Ctrl+V、または画像をドロップします。";
    std::wstring finalStatus = L"待機中";
    ct::SolveProgress shown;
    bool running = false;
    std::atomic<bool> cancel{false};
    std::thread worker;
    std::mutex mutex;
    bool pendingProgress = false, pendingResult = false;
    ct::SolveProgress progress;
    ct::SolveResult result;
    int uncertain = 0;
    bool geometryUncertain = false;
    bool runTimed = true, provenOptimal = false;
    double runBudget = 10.0;
    std::chrono::steady_clock::time_point runStarted{};
    bool smoke = false;
    double smokeSolve = -1.0;
    std::wstring capturePath;
    int smokeExitCode = 0;
};
int px(const App& a, int v) { return static_cast<int>(std::lround(v * a.scale)); }
void fill(HDC dc, RECT r, COLORREF c) { HBRUSH br = CreateSolidBrush(c); FillRect(dc, &r, br); DeleteObject(br); }
void box(HDC dc, RECT r, COLORREF color, int radius, COLORREF border = kBorder, int penWidth = 1) {
    HBRUSH br = CreateSolidBrush(color); HPEN pen = CreatePen(PS_SOLID, penWidth, border);
    auto oldBr = SelectObject(dc, br); auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBr); SelectObject(dc, oldPen); DeleteObject(br); DeleteObject(pen);
}
void text(HDC dc, const std::wstring& s, RECT r, HFONT font, COLORREF color = kInk,
          UINT flags = DT_LEFT | DT_TOP | DT_NOPREFIX) {
    auto old = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, flags); SelectObject(dc, old);
}
void set_control_font(HWND hwnd, HFONT font) { SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); }
HWND make_button(App& a, int id, const wchar_t* label) {
    HWND b = CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                             0, 0, 0, 0, a.hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    set_control_font(b, a.font); return b;
}
void create_fonts(App& a) {
    for (HFONT f : {a.font, a.fontSmall, a.fontTitle, a.fontSection, a.fontNumber}) if (f) DeleteObject(f);
    auto make = [&](int size, int weight) { return CreateFontW(-px(a, size), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI"); };
    a.font = make(14, FW_NORMAL); a.fontSmall = make(12, FW_NORMAL);
    a.fontTitle = make(26, FW_SEMIBOLD); a.fontSection = make(15, FW_SEMIBOLD); a.fontNumber = make(22, FW_SEMIBOLD);
}
void layout(App& a) {
    RECT rc; GetClientRect(a.hwnd, &rc);
    const int w = static_cast<int>(rc.right / a.scale), h = static_cast<int>(rc.bottom / a.scale);
    auto place = [&](int id, int x, int y, int cw, int ch) {
        MoveWindow(GetDlgItem(a.hwnd, id), px(a, x), px(a, y), px(a, cw), px(a, ch), TRUE);
    };
    place(Open, 24, 88, 122, 34); place(Paste, 154, 88, 126, 34); place(Example, 288, 88, 88, 34);
    place(ImageView, 384, 88, 106, 34); place(Calibrate, 498, 88, 106, 34); place(Edit, 612, 88, 106, 34);
    place(ImportBoard, 726, 88, 100, 34); place(SaveBoard, 834, 88, 88, 34);
    place(Mode, 77, 139, 151, 180); place(Limit, 238, 137, 79, 32);
    place(Start, 368, 135, 144, 38); place(Stop, 522, 135, 70, 38);
    const int paletteX = 618;
    for (int c = 0; c <= 10; ++c) place(kPaletteStart + c, paletteX + c * 29, 141, 25, 25);
    const int mainWidth = w - 380, cardHeight = h - 294;
    a.mainCard = {px(a, 24), px(a, 190), px(a, 24 + mainWidth), px(a, 190 + cardHeight)};
    a.sideCard = {px(a, w - 332), px(a, 190), px(a, w - 24), px(a, h - 28)};
    place(RouteList, w - 314, 380, 272, h - 469);
    place(Export, w - 314, h - 73, 272, 32);
    place(Reset, 24, h - 88, 86, 34); place(Previous, 120, h - 88, 86, 34);
    place(Next, 216, h - 88, 86, 34); place(Play, 312, h - 88, 112, 34);
    InvalidateRect(a.hwnd, nullptr, FALSE);
}
void update_enabled(App& a) {
    for (int id : {Open, Paste, Example, ImportBoard, Calibrate, Edit})
        EnableWindow(GetDlgItem(a.hwnd, id), !a.running && (id != Calibrate || !a.image.rgb.empty()));
    EnableWindow(GetDlgItem(a.hwnd, Start), a.haveBoard && !a.running);
    EnableWindow(GetDlgItem(a.hwnd, Stop), a.running);
    EnableWindow(a.mode, !a.running);
    EnableWindow(a.limit, !a.running && SendMessageW(a.mode, CB_GETCURSEL, 0, 0) == 0);
    EnableWindow(GetDlgItem(a.hwnd, SaveBoard), a.haveBoard && !a.running);
    EnableWindow(GetDlgItem(a.hwnd, ImageView), !a.image.rgb.empty());
    const bool has = !a.moves.empty();
    for (int id : {Reset, Previous, Next, Play, Export}) EnableWindow(GetDlgItem(a.hwnd, id), has);
    for (int c = 0; c <= 10; ++c) EnableWindow(GetDlgItem(a.hwnd, kPaletteStart + c), a.haveBoard && a.editMode && !a.running);
}
void stop_play(App& a) {
    a.playing = false; KillTimer(a.hwnd, kPlayTimer); SetWindowTextW(GetDlgItem(a.hwnd, Play), L"自動再生");
}
void clear_solution(App& a) {
    stop_play(a); a.moves.clear(); a.step = 0; a.shown = {}; a.finalStatus = L"待機中"; a.provenOptimal = false;
    SendMessageW(a.route, LB_RESETCONTENT, 0, 0); update_enabled(a);
}
ct::Board displayed_board(const App& a) {
    ct::Board b = a.original;
    for (int i = 0; i < a.step && i < static_cast<int>(a.moves.size()); ++i) {
        if (!ct::apply_move(b, a.moves[i].cell)) break;
    }
    return b;
}
bool same_route(const std::vector<ct::Move>& a, const std::vector<ct::Move>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].cell != b[i].cell) return false;
    return true;
}
void set_route(App& a, const std::vector<ct::Move>& moves) {
    if (same_route(a.moves, moves)) return;
    // Never display an unreplayable route, even if a worker or a file is malformed.
    ct::Board verify = a.original;
    std::vector<ct::Move> checked;
    for (const auto& m : moves) {
        ct::Move actual;
        if (!ct::apply_move(verify, m.cell, &actual)) return;
        checked.push_back(std::move(actual));
    }
    a.moves = std::move(checked); a.step = 0; stop_play(a);
    SendMessageW(a.route, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < a.moves.size(); ++i) {
        const auto& m = a.moves[i]; std::wostringstream s;
        s << std::setw(3) << std::setfill(L'0') << i + 1 << L"   行 " << std::setw(2) << m.cell / a.original.width + 1
          << L"  列 " << std::setw(2) << m.cell % a.original.width + 1 << L"    " << m.removed.size() << L"枚";
        SendMessageW(a.route, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.str().c_str()));
    }
    if (!a.moves.empty()) SendMessageW(a.route, LB_SETCURSEL, 0, 0);
    update_enabled(a);
}
void make_bgra(App& a) {
    a.imageBgra.resize(static_cast<size_t>(a.image.width) * a.image.height * 4);
    for (size_t i = 0; i < static_cast<size_t>(a.image.width) * a.image.height; ++i) {
        a.imageBgra[4 * i] = a.image.rgb[3 * i + 2]; a.imageBgra[4 * i + 1] = a.image.rgb[3 * i + 1];
        a.imageBgra[4 * i + 2] = a.image.rgb[3 * i]; a.imageBgra[4 * i + 3] = 255;
    }
}
void accept_recognition(App& a, ct::Recognition r) {
    clear_solution(a); a.recognition = std::move(r);
    a.geometryUncertain = a.recognition.message.find("Empty edge rows or columns") != std::string::npos;
    if (!a.recognition.ok) {
        a.haveBoard = false; a.headline = L"盤面を自動認識できませんでした";
        a.detail = L"「位置を調整」で左上座標とマスの大きさを指定してください。";
        a.showImage = true;
    } else {
        a.original = a.recognition.board; a.haveBoard = true; a.uncertain = a.recognition.uncertain_cells;
        a.headline = std::to_wstring(tile_count(a.original)) + L"枚のタイルを認識しました";
        a.detail = a.uncertain > 0 ? L"橙色の枠は認識が不確かなマスです。「盤面を修正」で確認してください。" : L"盤面を確認し、「最短手順を探索」を押してください。";
        if (a.geometryUncertain) a.detail = L"端の空白により位置がずれる可能性があります。「元画像を表示」で格子を確認してください。";
        a.showImage = false;
    }
    SetWindowTextW(GetDlgItem(a.hwnd, ImageView), a.showImage ? L"盤面を表示" : L"元画像を表示");
    update_enabled(a); InvalidateRect(a.hwnd, nullptr, FALSE);
}
bool load_image_file(const std::wstring& file, ct::Image& out, std::wstring& error) {
    IWICImagingFactory* factory = nullptr; IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr; IWICFormatConverter* converter = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    UINT width = 0, height = 0;
    if (SUCCEEDED(hr)) hr = frame->GetSize(&width, &height);
    if (SUCCEEDED(hr) && (width == 0 || height == 0 || width > 12000 || height > 12000 || static_cast<uint64_t>(width) * height > 50000000)) {
        hr = E_INVALIDARG; error = L"画像は最大5,000万画素、縦横12,000ピクセルまでにしてください。";
    }
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(frame, GUID_WICPixelFormat24bppRGB, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) {
        out.width = static_cast<int>(width); out.height = static_cast<int>(height); out.rgb.resize(static_cast<size_t>(width) * height * 3);
        hr = converter->CopyPixels(nullptr, width * 3, static_cast<UINT>(out.rgb.size()), out.rgb.data());
    }
    if (converter) converter->Release(); if (frame) frame->Release(); if (decoder) decoder->Release(); if (factory) factory->Release();
    if (FAILED(hr)) { out = {}; if (error.empty()) error = L"画像を開けませんでした。PNG、JPEG、BMP形式の画像を指定してください。"; return false; }
    return true;
}
void open_path(App& a, const std::wstring& file) {
    if (a.running) return;
    ct::Image im; std::wstring error;
    if (!load_image_file(file, im, error)) { MessageBoxW(a.hwnd, error.c_str(), L"画像の読み込み", MB_OK | MB_ICONWARNING); return; }
    a.image = std::move(im); a.imageName = std::filesystem::path(file).filename().wstring(); make_bgra(a);
    accept_recognition(a, ct::recognize(a.image));
}
std::wstring choose_file(HWND hwnd, bool save, const wchar_t* filter, const wchar_t* ext, const wchar_t* suggested = L"") {
    wchar_t buffer[32768] = {}; lstrcpynW(buffer, suggested, static_cast<int>(std::size(buffer)));
    OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = buffer; ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
    ofn.lpstrFilter = filter; ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::wstring(buffer) : std::wstring();
}
void open_dialog(App& a) {
    const auto file = choose_file(a.hwnd, false, L"画像ファイル\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0すべてのファイル\0*.*\0\0", L"png");
    if (!file.empty()) open_path(a, file);
}
bool bitmap_to_image(HBITMAP bitmap, ct::Image& out) {
    BITMAP bm{}; if (!GetObjectW(bitmap, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return false;
    if (bm.bmWidth > 12000 || bm.bmHeight > 12000 || static_cast<uint64_t>(bm.bmWidth) * bm.bmHeight > 50000000) return false;
    out.width = bm.bmWidth; out.height = bm.bmHeight;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = out.width; bi.bmiHeader.biHeight = -out.height;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> pixels(static_cast<size_t>(out.width) * out.height * 4);
    HDC dc = GetDC(nullptr); const bool ok = GetDIBits(dc, bitmap, 0, out.height, pixels.data(), &bi, DIB_RGB_COLORS) == out.height; ReleaseDC(nullptr, dc);
    if (!ok) return false;
    out.rgb.resize(static_cast<size_t>(out.width) * out.height * 3);
    for (size_t i = 0; i < static_cast<size_t>(out.width) * out.height; ++i) {
        out.rgb[3 * i] = pixels[4 * i + 2]; out.rgb[3 * i + 1] = pixels[4 * i + 1]; out.rgb[3 * i + 2] = pixels[4 * i];
    }
    return true;
}
void paste_image(App& a) {
    if (a.running) return;
    if (!OpenClipboard(a.hwnd)) return;
    ct::Image im; bool ok = false;
    // CF_BITMAP is synthesized by Windows for CF_DIB and CF_DIBV5 clipboard data.
    HBITMAP bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    if (bitmap) ok = bitmap_to_image(bitmap, im);
    CloseClipboard();
    if (!ok) { MessageBoxW(a.hwnd, L"クリップボードに画像がありません。Windowsの切り取り機能などで画像をコピーしてください。", L"画像の貼り付け", MB_OK | MB_ICONINFORMATION); return; }
    a.image = std::move(im); a.imageName = L"クリップボード"; make_bgra(a); accept_recognition(a, ct::recognize(a.image));
}
void start_solver(App& a) {
    if (a.running || !a.haveBoard) return;
    std::string error;
    if (!ct::validate_board(a.original, &error)) { MessageBoxW(a.hwnd, wide(error).c_str(), L"盤面を確認してください", MB_OK | MB_ICONWARNING); return; }
    if ((a.uncertain > 0 || a.geometryUncertain) && MessageBoxW(a.hwnd, L"盤面の色または位置に不確かな箇所があります。元画像と格子を確認してください。この盤面で探索しますか？", L"認識結果の確認", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    const bool timed = SendMessageW(a.mode, CB_GETCURSEL, 0, 0) == 0;
    wchar_t seconds[64]{}; GetWindowTextW(a.limit, seconds, 64); wchar_t* end = nullptr;
    double budget = wcstod(seconds, &end);
    if (timed && (end == seconds || *end != 0 || !std::isfinite(budget) || budget < 0.1 || budget > 86400)) {
        MessageBoxW(a.hwnd, L"時間は0.1～86,400秒の数値で入力してください。小数も指定できます。", L"探索時間", MB_OK | MB_ICONWARNING); return;
    }
    if (a.worker.joinable()) a.worker.join();
    clear_solution(a); a.editMode = false; SetWindowTextW(GetDlgItem(a.hwnd, Edit), L"盤面を修正");
    a.running = true; a.cancel.store(false); a.runTimed = timed; a.runBudget = timed ? budget : 0;
    a.runStarted = std::chrono::steady_clock::now(); a.shown.best_remaining = tile_count(a.original);
    a.headline = L"全消しの手順を探索しています"; a.detail = L"暫定の手順は探索中でも確認できます。停止すると、その時点の結果を残します。";
    a.finalStatus = L"探索中"; update_enabled(a);
    ct::SolveOptions options; options.time_limit_seconds = a.runBudget; options.prove_optimal = true;
    options.threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    const auto board = a.original;
    a.worker = std::thread([&a, board, options]() {
        auto callback = [&a](const ct::SolveProgress& p) {
            std::lock_guard<std::mutex> lock(a.mutex); a.progress = p; a.pendingProgress = true;
        };
        ct::SolveResult r;
        try { r = ct::solve(board, options, a.cancel, callback); }
        catch (const std::exception& e) { r.status = ct::SolveStatus::Invalid; r.message = e.what(); r.remaining = tile_count(board); }
        catch (...) { r.status = ct::SolveStatus::Invalid; r.message = "Unexpected solver error"; r.remaining = tile_count(board); }
        { std::lock_guard<std::mutex> lock(a.mutex); a.result = std::move(r); a.pendingResult = true; }
        PostMessageW(a.hwnd, kWorkerDone, 0, 0);
    });
    InvalidateRect(a.hwnd, nullptr, FALSE);
}
void consume_worker(App& a) {
    ct::SolveProgress p; ct::SolveResult r; bool gotProgress, gotResult;
    { std::lock_guard<std::mutex> lock(a.mutex); gotProgress = a.pendingProgress; gotResult = a.pendingResult;
      if (gotProgress) { p = std::move(a.progress); a.pendingProgress = false; }
      if (gotResult) { r = std::move(a.result); a.pendingResult = false; } }
    if (gotProgress) { a.shown = p; set_route(a, p.moves);
        if (p.best_remaining == 0) { a.headline = L"全消しの手順を発見しました"; a.detail = L"さらに短い手順の探索と、最短性の確認を続けています。"; a.finalStatus = L"全消し暫定解"; }
    }
    if (gotResult) {
        a.running = false; if (a.worker.joinable()) a.worker.join(); set_route(a, r.moves);
        a.shown.nodes = r.nodes; a.shown.elapsed = r.seconds; a.shown.lower_bound = r.lower_bound;
        a.shown.best_remaining = r.remaining; a.shown.best_moves = static_cast<int>(r.moves.size());
        a.provenOptimal = r.status == ct::SolveStatus::Optimal;
        switch (r.status) {
        case ct::SolveStatus::Optimal: a.finalStatus = L"最短確定 " + std::to_wstring(r.moves.size()) + L"手"; a.headline = L"最短の全消し手順を確定しました";
            a.detail = L"これより少ないクリック数での全消しはできないことを確認しました。"; break;
        case ct::SolveStatus::Feasible:
            a.finalStatus = a.runTimed ? budget_string(a.runBudget) + L"秒以内の最少手数" : L"全消し暫定解";
            a.headline = (a.runTimed ? budget_string(a.runBudget) + L"秒以内の最少手数: " : L"全消し暫定解: ") + std::to_wstring(r.moves.size()) + L"手（最短性未確認）";
            a.detail = L"表示は探索で見つかった全消し手順の最少手数です。さらに短い手順が存在する可能性があります。"; break;
        case ct::SolveStatus::Partial: a.finalStatus = L"時間内に全消し未発見"; a.headline = L"時間内に全消し未発見 / 最良の残り " + std::to_wstring(r.remaining) + L"枚";
            a.detail = L"表示しているのは見つかった部分手順です。全消し不能を意味しません。"; break;
        case ct::SolveStatus::Unsolvable: a.finalStatus = L"全消し不能"; a.headline = L"この盤面は全消しできません";
            a.detail = L"認識した盤面について全消し不能と判定しました。画像と色を確認してください。"; break;
        case ct::SolveStatus::Cancelled: a.finalStatus = r.remaining == 0 ? L"停止／全消し暫定解" : L"探索を停止";
            a.headline = L"探索を停止しました"; a.detail = L"停止までに見つかった手順を表示しています。最短性は未確認です。"; break;
        default: a.finalStatus = L"エラー"; a.headline = L"探索を開始できませんでした"; a.detail = wide(r.message); break;
        }
        update_enabled(a);
    }
    if (gotProgress || gotResult) InvalidateRect(a.hwnd, nullptr, FALSE);
}
void set_step(App& a, int step) {
    a.step = std::clamp(step, 0, static_cast<int>(a.moves.size()));
    if (a.step < static_cast<int>(a.moves.size())) SendMessageW(a.route, LB_SETCURSEL, a.step, 0);
    else SendMessageW(a.route, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    InvalidateRect(a.hwnd, nullptr, FALSE);
}
void export_route(App& a) {
    if (a.moves.empty()) return;
    auto file = choose_file(a.hwnd, true, L"CSVファイル\0*.csv\0\0", L"csv", L"colortile_solution.csv");
    if (file.empty()) return;
    std::ofstream out(std::filesystem::path(file), std::ios::binary);
    out << "\xEF\xBB\xBF";
    out << "# mode," << (a.runTimed ? "timed" : "until_optimal") << "\r\n";
    out << std::setprecision(12) << "# time_budget_seconds," << a.runBudget << "\r\n";
    out << "# elapsed_seconds," << a.shown.elapsed << "\r\n";
    out << "# optimality_proven," << (a.provenOptimal ? "true" : "false") << "\r\n";
    out << "# all_tiles_cleared," << (a.shown.best_remaining == 0 ? "true" : "false") << "\r\n";
    out << "step,row,column,removed_count,remaining_tiles,image_x,image_y,status\r\n";
    auto board = a.original;
    for (size_t i = 0; i < a.moves.size(); ++i) {
        ct::Move m; if (!ct::apply_move(board, a.moves[i].cell, &m)) break;
        int row = m.cell / board.width, col = m.cell % board.width;
        out << i + 1 << ',' << row + 1 << ',' << col + 1 << ',' << m.removed.size() << ',' << tile_count(board) << ',';
        if (!a.image.rgb.empty()) out << std::fixed << std::setprecision(2) << a.recognition.grid.left + (col + 0.5) * a.recognition.grid.cell_width;
        out << ',';
        if (!a.image.rgb.empty()) out << std::fixed << std::setprecision(2) << a.recognition.grid.top + (row + 0.5) * a.recognition.grid.cell_height;
        out << ',' << utf8(a.finalStatus) << "\r\n";
    }
    out.close();
    if (!out) MessageBoxW(a.hwnd, L"ファイルを書き込めませんでした。", L"CSV保存", MB_OK | MB_ICONERROR);
    else { a.detail = L"手順をCSVファイルに保存しました。行と列は1から数えます。"; InvalidateRect(a.hwnd, nullptr, FALSE); }
}
void save_board(App& a) {
    auto file = choose_file(a.hwnd, true, L"盤面ファイル\0*.ctb;*.txt\0\0", L"ctb", L"colortile_board.ctb"); if (file.empty()) return;
    std::ofstream out{std::filesystem::path(file)};
    out << a.original.width << ' ' << a.original.height << '\n';
    for (int y = 0; y < a.original.height; ++y) {
        for (int x = 0; x < a.original.width; ++x) out << (x ? " " : "") << static_cast<int>(a.original.cells[y * a.original.width + x]);
        out << '\n';
    }
    out.close(); if (!out) MessageBoxW(a.hwnd, L"ファイルを書き込めませんでした。", L"盤面保存", MB_OK | MB_ICONERROR);
}
void import_board_path(App& a, const std::wstring& file) {
    if (file.empty() || a.running) return;
    std::ifstream in{std::filesystem::path(file)}; ct::Board b;
    if (!(in >> b.width >> b.height) || b.width < 1 || b.height < 1 || b.width > 32 || b.height > 32) {
        MessageBoxW(a.hwnd, L"先頭行に列数と行数、以降に色番号0～10を記載してください。", L"盤面の読み込み", MB_OK | MB_ICONERROR); return;
    }
    b.cells.clear(); int c; bool validFields = true;
    while (in >> c) { if (c < 0 || c > 10) { validFields = false; break; } b.cells.push_back(static_cast<uint8_t>(c)); }
    std::string error;
    if (!validFields || !ct::validate_board(b, &error) || !in.eof()) { MessageBoxW(a.hwnd, L"盤面テキストが不正です。行数、列数、色番号を確認してください。", L"盤面の読み込み", MB_OK | MB_ICONERROR); return; }
    a.image = {}; a.imageBgra.clear(); a.recognition = {}; a.recognition.ok = true; a.recognition.board = b;
    a.recognition.grid.columns = b.width; a.recognition.grid.rows = b.height; a.recognition.confidence.assign(b.cells.size(), 1);
    accept_recognition(a, a.recognition); a.imageName = std::filesystem::path(file).filename().wstring();
}
void import_board(App& a) {
    const auto file = choose_file(a.hwnd, false, L"盤面ファイル\0*.ctb;*.txt\0\0", L"ctb");
    if (!file.empty()) import_board_path(a, file);
}
struct CalibrationState { App* app; HWND hwnd; HWND edits[6]{}; bool applied = false; };
LRESULT CALLBACK calibration_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<CalibrationState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) { state = static_cast<CalibrationState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); state->hwnd = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); }
    if (!state) return DefWindowProcW(hwnd, msg, wp, lp);
    App& a = *state->app;
    switch (msg) {
    case WM_CREATE: {
        const wchar_t* labels[] = {L"左上 X（画像内 px）", L"左上 Y（画像内 px）", L"1マスの幅（px）", L"1マスの高さ（px）", L"列数", L"行数"};
        const auto& g = a.recognition.grid;
        double values[] = {g.left, g.top, g.cell_width, g.cell_height, static_cast<double>(g.columns), static_cast<double>(g.rows)};
        for (int i = 0; i < 6; ++i) {
            HWND label = CreateWindowW(L"STATIC", labels[i], WS_CHILD | WS_VISIBLE, px(a, 22), px(a, 22 + i * 38), px(a, 192), px(a, 26), hwnd, nullptr, nullptr, nullptr); set_control_font(label, a.font);
            std::wostringstream value; value << std::fixed << std::setprecision(i < 4 ? 2 : 0) << values[i];
            state->edits[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.str().c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                px(a, 221), px(a, 18 + i * 38), px(a, 120), px(a, 29), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1000 + i)), nullptr, nullptr); set_control_font(state->edits[i], a.font);
        }
        HWND note = CreateWindowW(L"STATIC", L"座標は盤面外枠の左上です。マスの中央ではありません。", WS_CHILD | WS_VISIBLE, px(a, 22), px(a, 254), px(a, 340), px(a, 42), hwnd, nullptr, nullptr, nullptr); set_control_font(note, a.fontSmall);
        HWND ok = CreateWindowW(L"BUTTON", L"この位置で読み取る", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, px(a, 22), px(a, 308), px(a, 182), px(a, 36), hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr); set_control_font(ok, a.font);
        HWND cancel = CreateWindowW(L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE | WS_TABSTOP, px(a, 216), px(a, 308), px(a, 125), px(a, 36), hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr); set_control_font(cancel, a.font);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        if (LOWORD(wp) == IDOK) {
            double v[6]{}; bool valid = true;
            for (int i = 0; i < 6; ++i) { wchar_t buf[64]{}; GetWindowTextW(state->edits[i], buf, 64); wchar_t* end = nullptr; v[i] = wcstod(buf, &end); valid &= end != buf && *end == 0 && std::isfinite(v[i]); }
            valid &= v[0] >= 0 && v[1] >= 0 && v[2] >= 3 && v[3] >= 3 && v[4] >= 1 && v[4] <= 32 && v[5] >= 1 && v[5] <= 32 && std::floor(v[4]) == v[4] && std::floor(v[5]) == v[5];
            valid &= v[0] + v[2] * v[4] <= a.image.width + 0.01 && v[1] + v[3] * v[5] <= a.image.height + 0.01;
            if (!valid) { MessageBoxW(hwnd, L"画像内に収まる正しい座標、マスの大きさ、整数の行列数を指定してください。", L"位置の調整", MB_OK | MB_ICONWARNING); return 0; }
            ct::GridGeometry g; g.left = v[0]; g.top = v[1]; g.cell_width = v[2]; g.cell_height = v[3]; g.columns = static_cast<int>(v[4]); g.rows = static_cast<int>(v[5]);
            accept_recognition(a, ct::sample_grid(a.image, g)); state->applied = true; DestroyWindow(hwnd); return 0;
        } break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void calibration_dialog(App& a) {
    if (a.running || a.image.rgb.empty()) return;
    CalibrationState state{&a, nullptr};
    RECT parent; GetWindowRect(a.hwnd, &parent);
    const int w = px(a, 390), h = px(a, 405);
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kCalibrationClass, L"盤面の位置を調整", WS_CAPTION | WS_SYSMENU | WS_POPUP,
        parent.left + (parent.right - parent.left - w) / 2, parent.top + (parent.bottom - parent.top - h) / 2, w, h, a.hwnd, nullptr, GetModuleHandleW(nullptr), &state);
    if (!dialog) return;
    EnableWindow(a.hwnd, FALSE); ShowWindow(dialog, SW_SHOW); SetFocus(state.edits[0]);
    MSG msg{};
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(a.hwnd, TRUE); SetActiveWindow(a.hwnd);
}
void draw_image(App& a, HDC dc, RECT r) {
    if (a.imageBgra.empty()) return;
    const double s = std::min(static_cast<double>(r.right - r.left) / a.image.width, static_cast<double>(r.bottom - r.top) / a.image.height);
    const int w = static_cast<int>(a.image.width * s), h = static_cast<int>(a.image.height * s);
    const int x = r.left + (r.right - r.left - w) / 2, y = r.top + (r.bottom - r.top - h) / 2;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = a.image.width; bi.bmiHeader.biHeight = -a.image.height;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, x, y, w, h, 0, 0, a.image.width, a.image.height, a.imageBgra.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    if (a.recognition.ok) {
        const auto& g = a.recognition.grid;
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(50, 108, 225)); auto old = SelectObject(dc, pen);
        for (int i = 0; i <= g.columns; ++i) { int xx = x + static_cast<int>((g.left + i * g.cell_width) * s); MoveToEx(dc, xx, y + static_cast<int>(g.top * s), nullptr); LineTo(dc, xx, y + static_cast<int>((g.top + g.rows * g.cell_height) * s)); }
        for (int i = 0; i <= g.rows; ++i) { int yy = y + static_cast<int>((g.top + i * g.cell_height) * s); MoveToEx(dc, x + static_cast<int>(g.left * s), yy, nullptr); LineTo(dc, x + static_cast<int>((g.left + g.columns * g.cell_width) * s), yy); }
        SelectObject(dc, old); DeleteObject(pen);
    }
}
void draw_board(App& a, HDC dc, RECT r) {
    const ct::Board b = displayed_board(a);
    a.cellSize = std::min(static_cast<double>(r.right - r.left - px(a, 26)) / b.width,
                          static_cast<double>(r.bottom - r.top - px(a, 25)) / b.height);
    int width = static_cast<int>(std::round(a.cellSize * b.width)), height = static_cast<int>(std::round(a.cellSize * b.height));
    a.boardRect = {r.left + (r.right - r.left - width) / 2 + px(a, 9), r.top + (r.bottom - r.top - height) / 2 + px(a, 9), 0, 0};
    a.boardRect.right = a.boardRect.left + width; a.boardRect.bottom = a.boardRect.top + height;
    for (int y = 0; y < b.height; ++y) for (int x = 0; x < b.width; ++x) {
        RECT cell{a.boardRect.left + static_cast<int>(std::round(x * a.cellSize)), a.boardRect.top + static_cast<int>(std::round(y * a.cellSize)),
                  a.boardRect.left + static_cast<int>(std::round((x + 1) * a.cellSize)), a.boardRect.top + static_cast<int>(std::round((y + 1) * a.cellSize))};
        fill(dc, cell, ((x + y) & 1) ? RGB(237, 241, 246) : RGB(247, 249, 252));
        int index = y * b.width + x, c = b.cells[index];
        if (c) {
            const int inset = std::max(2, static_cast<int>(a.cellSize * 0.08)); InflateRect(&cell, -inset, -inset);
            box(dc, cell, palette_color(c), px(a, 5), palette_color(c));
            if (a.editMode && a.cellSize >= px(a, 22)) text(dc, std::to_wstring(c), cell, a.fontSmall, c == 2 || c == 6 ? RGB(255, 255, 255) : kInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (a.step == 0 && index < static_cast<int>(a.recognition.confidence.size()) && a.recognition.confidence[index] < 0.70f) {
            HPEN pen = CreatePen(PS_SOLID, px(a, 2), RGB(224, 133, 18)); auto oldPen = SelectObject(dc, pen); auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, cell.left, cell.top, cell.right, cell.bottom); SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen);
        }
    }
    for (int x = 0; x < b.width; ++x) {
        RECT t{a.boardRect.left + static_cast<int>(x * a.cellSize), a.boardRect.top - px(a, 22), a.boardRect.left + static_cast<int>((x + 1) * a.cellSize), a.boardRect.top - px(a, 2)};
        text(dc, std::to_wstring(x + 1), t, a.fontSmall, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    for (int y = 0; y < b.height; ++y) {
        RECT t{a.boardRect.left - px(a, 27), a.boardRect.top + static_cast<int>(y * a.cellSize), a.boardRect.left - px(a, 4), a.boardRect.top + static_cast<int>((y + 1) * a.cellSize)};
        text(dc, std::to_wstring(y + 1), t, a.fontSmall, kMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!a.editMode && a.step < static_cast<int>(a.moves.size())) {
        const auto& move = a.moves[a.step];
        auto center = [&](int cell) { return POINT{a.boardRect.left + static_cast<int>((cell % b.width + 0.5) * a.cellSize), a.boardRect.top + static_cast<int>((cell / b.width + 0.5) * a.cellSize)}; };
        POINT click = center(move.cell);
        HPEN pen = CreatePen(PS_DOT, 1, kAccent); auto oldPen = SelectObject(dc, pen);
        for (int removed : move.removed) { POINT p = center(removed); MoveToEx(dc, click.x, click.y, nullptr); LineTo(dc, p.x, p.y); }
        SelectObject(dc, oldPen); DeleteObject(pen);
        for (int removed : move.removed) {
            POINT p = center(removed); int rad = std::max(3, static_cast<int>(a.cellSize * 0.44));
            RECT tile{p.x - rad, p.y - rad, p.x + rad, p.y + rad};
            HPEN border = CreatePen(PS_SOLID, px(a, 2), kInk); auto old = SelectObject(dc, border); auto brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            RoundRect(dc, tile.left, tile.top, tile.right, tile.bottom, px(a, 5), px(a, 5)); SelectObject(dc, old); SelectObject(dc, brush); DeleteObject(border);
        }
        int radius = std::max(px(a, 9), static_cast<int>(a.cellSize * 0.43));
        HBRUSH br = CreateSolidBrush(kAccent); auto oldBr = SelectObject(dc, br); HPEN outline = CreatePen(PS_SOLID, px(a, 2), RGB(255, 255, 255)); oldPen = SelectObject(dc, outline);
        Ellipse(dc, click.x - radius, click.y - radius, click.x + radius, click.y + radius); SelectObject(dc, oldBr); SelectObject(dc, oldPen); DeleteObject(br); DeleteObject(outline);
        RECT label{click.x - radius, click.y - radius, click.x + radius, click.y + radius}; text(dc, L"＋", label, a.fontSection, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}
void paint(App& a) {
    PAINTSTRUCT ps; HDC screen = BeginPaint(a.hwnd, &ps); RECT rc; GetClientRect(a.hwnd, &rc);
    HDC dc = CreateCompatibleDC(screen); HBITMAP bitmap = CreateCompatibleBitmap(screen, std::max(1L, rc.right), std::max(1L, rc.bottom)); auto oldBitmap = SelectObject(dc, bitmap);
    fill(dc, rc, kBackground);
    text(dc, L"ColorTile Solver", {px(a, 24), px(a, 18), rc.right - px(a, 24), px(a, 52)}, a.fontTitle);
    text(dc, L"画像から盤面を読み取り、全消しの最短手順を探索", {px(a, 26), px(a, 55), rc.right - px(a, 24), px(a, 78)}, a.fontSmall, kMuted);
    text(dc, L"モード", {px(a, 26), px(a, 144), px(a, 77), px(a, 168)}, a.font, kMuted);
    text(dc, L"秒", {px(a, 326), px(a, 144), px(a, 359), px(a, 168)}, a.font, kMuted);
    box(dc, a.mainCard, RGB(255, 255, 255), px(a, 12)); box(dc, a.sideCard, RGB(255, 255, 255), px(a, 12));
    RECT mainTitle{a.mainCard.left + px(a, 20), a.mainCard.top + px(a, 17), a.mainCard.right - px(a, 20), a.mainCard.top + px(a, 40)};
    std::wstring title = a.showImage ? L"元画像と認識位置" : a.editMode ? L"盤面を修正（色を選んでマスをクリック）" : L"盤面と次のクリック位置";
    text(dc, title, mainTitle, a.fontSection);
    RECT content{a.mainCard.left + px(a, 20), a.mainCard.top + px(a, 57), a.mainCard.right - px(a, 20), a.mainCard.bottom - px(a, 18)};
    if (a.showImage && !a.imageBgra.empty()) draw_image(a, dc, content);
    else if (a.haveBoard) draw_board(a, dc, content);
    else {
        RECT blank{content.left + px(a, 18), content.top + px(a, 45), content.right - px(a, 18), content.bottom};
        text(dc, L"画像をここにドロップ", blank, a.fontNumber, kMuted, DT_CENTER | DT_TOP);
        blank.top += px(a, 42); text(dc, L"PNG / JPEG / BMP\n\nCtrl+V でスクリーンショットを貼り付けられます。\n「サンプル」から動作を試せます。", blank, a.font, kMuted, DT_CENTER | DT_TOP | DT_WORDBREAK);
    }
    RECT side{a.sideCard.left + px(a, 18), a.sideCard.top + px(a, 15), a.sideCard.right - px(a, 18), a.sideCard.top + px(a, 40)};
    COLORREF statusColor = a.provenOptimal ? RGB(16, 136, 83) : kAccent;
    text(dc, a.finalStatus, side, a.fontSection, statusColor);
    side.top += px(a, 35); side.bottom = side.top + px(a, 35);
    const int total = a.haveBoard ? tile_count(a.original) : 0;
    const int remain = a.haveBoard ? tile_count(displayed_board(a)) : 0;
    std::wstring primary = a.moves.empty() ? std::to_wstring(total) + L" 枚" : std::to_wstring(a.moves.size()) + L" 手";
    text(dc, primary, side, a.fontNumber);
    side.top += px(a, 38); side.bottom = side.top + px(a, 22);
    std::wstring bound = L"手数の下界 " + std::to_wstring(a.shown.lower_bound) + L"  /  再生中の残り " + std::to_wstring(remain) + L" 枚";
    text(dc, bound, side, a.fontSmall, kMuted);
    side.top += px(a, 26); side.bottom = side.top + px(a, 22);
    const double elapsed = a.running ? std::chrono::duration<double>(std::chrono::steady_clock::now() - a.runStarted).count() : a.shown.elapsed;
    const std::wstring timing = a.running && a.runTimed ? L"残り " + seconds_string(std::max(0.0, a.runBudget - elapsed)) + L"秒" : L"経過 " + seconds_string(elapsed) + L"秒";
    text(dc, timing + L"  /  探索 " + count_string(a.shown.nodes), side, a.fontSmall, kMuted);
    side.top += px(a, 31); side.bottom = side.top + px(a, 25);
    text(dc, L"手順一覧（行と列は1から）", side, a.fontSection);
    const int bottom = static_cast<int>(rc.bottom / a.scale);
    std::wstring stepInfo = a.moves.empty() ? L"" : std::to_wstring(a.step) + L" / " + std::to_wstring(a.moves.size()) + L" 手を表示";
    text(dc, stepInfo, {px(a, 440), px(a, bottom - 81), a.mainCard.right, px(a, bottom - 52)}, a.fontSmall, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, a.headline, {px(a, 26), px(a, bottom - 44), a.mainCard.right, px(a, bottom - 23)}, a.fontSmall, kInk, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    text(dc, a.detail, {px(a, 26), px(a, bottom - 23), a.mainCard.right, px(a, bottom - 3)}, a.fontSmall, kMuted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    BitBlt(screen, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc); EndPaint(a.hwnd, &ps);
}
void draw_button(App& a, DRAWITEMSTRUCT* d) {
    const int id = d->CtlID; RECT r = d->rcItem; const bool disabled = (d->itemState & ODS_DISABLED) != 0;
    const bool pressed = (d->itemState & ODS_SELECTED) != 0;
    if (id >= kPaletteStart && id < kPaletteStart + 11) {
        const int c = id - kPaletteStart; InflateRect(&r, -1, -1);
        COLORREF color = palette_color(c);
        if (disabled) color = RGB((GetRValue(color) + 255 * 2) / 3, (GetGValue(color) + 255 * 2) / 3, (GetBValue(color) + 255 * 2) / 3);
        box(d->hDC, r, color, px(a, 5), c == a.selectedColor && a.editMode ? kInk : kBorder, c == a.selectedColor && a.editMode ? px(a, 2) : 1);
        if (c == 0) text(d->hDC, L"×", r, a.font, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    bool accent = id == Start || (id == Edit && a.editMode);
    COLORREF bg = disabled ? RGB(237, 241, 247) : accent ? pressed ? RGB(23, 80, 178) : kAccent : pressed ? RGB(225, 233, 245) : RGB(255, 255, 255);
    box(d->hDC, r, bg, px(a, 7), accent && !disabled ? bg : kBorder);
    wchar_t label[128]{}; GetWindowTextW(d->hwndItem, label, 128);
    text(d->hDC, label, r, a.font, disabled ? RGB(158, 170, 187) : accent ? RGB(255, 255, 255) : kInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (d->itemState & ODS_FOCUS) { InflateRect(&r, -px(a, 4), -px(a, 4)); DrawFocusRect(d->hDC, &r); }
}
int hit_cell(const App& a, int x, int y) {
    if (!a.haveBoard || a.showImage || x < a.boardRect.left || x >= a.boardRect.right || y < a.boardRect.top || y >= a.boardRect.bottom) return -1;
    int c = static_cast<int>((x - a.boardRect.left) / a.cellSize), r = static_cast<int>((y - a.boardRect.top) / a.cellSize);
    if (c < 0 || c >= a.original.width || r < 0 || r >= a.original.height) return -1;
    return r * a.original.width + c;
}
void edit_cell(App& a, int cell, int color) {
    if (cell < 0 || a.running) return;
    clear_solution(a); a.original.cells[cell] = static_cast<uint8_t>(color);
    if (a.recognition.confidence.size() == a.original.cells.size()) a.recognition.confidence[cell] = 1;
    a.uncertain = static_cast<int>(std::count_if(a.recognition.confidence.begin(), a.recognition.confidence.end(), [](float c) { return c < 0.70f; }));
    a.headline = L"盤面を修正しました  /  " + std::to_wstring(tile_count(a.original)) + L" 枚";
    a.detail = L"修正が終わったら「修正を終了」または「最短手順を探索」を押してください。";
    update_enabled(a); InvalidateRect(a.hwnd, nullptr, FALSE);
}
void load_example(App& a) {
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768);
    std::filesystem::path base = std::filesystem::path(path).parent_path();
    for (const auto& candidate : {base / L"examples" / L"sample.png", base / L"sample.png"})
        if (std::filesystem::exists(candidate)) { open_path(a, candidate.wstring()); return; }
    MessageBoxW(a.hwnd, L"examples\\sample.png が見つかりません。ZIPをすべて展開してから起動してください。", L"サンプル画像", MB_OK | MB_ICONINFORMATION);
}
bool capture_client(App& a) {
    if (a.capturePath.empty()) return true;
    RedrawWindow(a.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    RECT r{}; GetClientRect(a.hwnd, &r);
    const int w = r.right, h = r.bottom;
    if (w <= 0 || h <= 0) return false;
    POINT origin{0, 0}; ClientToScreen(a.hwnd, &origin);
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
    auto old = SelectObject(memory, bitmap);
    const bool copied = BitBlt(memory, 0, 0, w, h, screen, origin.x, origin.y, SRCCOPY) != 0;
    SelectObject(memory, old);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    bool ok = copied && GetDIBits(memory, bitmap, 0, h, pixels.data(), &bi, DIB_RGB_COLORS) == h;
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
    if (!ok) return false;
    BITMAPFILEHEADER header{}; header.bfType = 0x4D42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream out{std::filesystem::path(a.capturePath), std::ios::binary};
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(BITMAPINFOHEADER));
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    out.close();
    std::ofstream diagnostic{std::filesystem::path(a.capturePath + L".json")};
    diagnostic << std::setprecision(12) << "{\n  \"recognized_tiles\": " << (a.haveBoard ? tile_count(a.original) : 0)
               << ",\n  \"uncertain_cells\": " << a.uncertain
               << ",\n  \"solution_steps\": " << a.moves.size()
               << ",\n  \"best_remaining\": " << a.shown.best_remaining
               << ",\n  \"optimality_proven\": " << (a.provenOptimal ? "true" : "false")
               << ",\n  \"time_budget_seconds\": " << a.runBudget
               << ",\n  \"elapsed_seconds\": " << a.shown.elapsed
               << ",\n  \"nodes\": " << a.shown.nodes
               << ",\n  \"status\": \"" << utf8(a.finalStatus) << "\"\n}\n";
    return out.good() && diagnostic.good();
}
LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* a = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) { a = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); a->hwnd = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a)); }
    if (!a) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        HDC dc = GetDC(hwnd); a->scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0; ReleaseDC(hwnd, dc); create_fonts(*a);
        for (auto item : std::initializer_list<std::pair<int, const wchar_t*>>{
            {Open, L"画像を開く"}, {Paste, L"貼り付け Ctrl+V"}, {Example, L"サンプル"}, {ImageView, L"元画像を表示"},
            {Calibrate, L"位置を調整"}, {Edit, L"盤面を修正"}, {ImportBoard, L"盤面を開く"}, {SaveBoard, L"盤面保存"},
            {Start, L"最短手順を探索"}, {Stop, L"停止"}, {Reset, L"最初へ"}, {Previous, L"前へ"}, {Next, L"次へ"},
            {Play, L"自動再生"}, {Export, L"手順をCSVで保存"}}) make_button(*a, item.first, item.second);
        for (int c = 0; c <= 10; ++c) make_button(*a, kPaletteStart + c, kColorNames[c]);
        a->mode = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(Mode), GetModuleHandleW(nullptr), nullptr); set_control_font(a->mode, a->font);
        for (const wchar_t* s : {L"時間を指定して探索", L"最短確定まで探索"}) SendMessageW(a->mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
        SendMessageW(a->mode, CB_SETCURSEL, 0, 0);
        a->limit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10.0", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(Limit), GetModuleHandleW(nullptr), nullptr); set_control_font(a->limit, a->font);
        a->route = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(RouteList), GetModuleHandleW(nullptr), nullptr); set_control_font(a->route, a->font);
        DragAcceptFiles(hwnd, TRUE); SetTimer(hwnd, kUiTimer, 100, nullptr); layout(*a); update_enabled(*a); return 0;
    }
    case WM_SIZE: layout(*a); return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor); GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
        info->ptMinTrackSize.x = std::min(px(*a, 960), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
        info->ptMinTrackSize.y = std::min(px(*a, 720), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top)); return 0;
    }
    case WM_DPICHANGED: {
        a->scale = HIWORD(wp) / 96.0; create_fonts(*a);
        EnumChildWindows(hwnd, [](HWND child, LPARAM context) -> BOOL { set_control_font(child, reinterpret_cast<App*>(context)->font); return TRUE; }, reinterpret_cast<LPARAM>(a));
        auto* r = reinterpret_cast<RECT*>(lp); SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); layout(*a); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(*a); return 0;
    case WM_DRAWITEM: draw_button(*a, reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
    case WM_CTLCOLORLISTBOX: SetTextColor(reinterpret_cast<HDC>(wp), kInk); SetBkColor(reinterpret_cast<HDC>(wp), RGB(255, 255, 255)); return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= kPaletteStart && id < kPaletteStart + 11) { a->selectedColor = id - kPaletteStart; for (int c = 0; c <= 10; ++c) InvalidateRect(GetDlgItem(hwnd, kPaletteStart + c), nullptr, TRUE); return 0; }
        if (a->running && (id == Open || id == Paste || id == Example || id == ImportBoard || id == Calibrate || id == Edit || id == SaveBoard)) return 0;
        switch (id) {
        case Mode: if (HIWORD(wp) == CBN_SELCHANGE) update_enabled(*a); break;
        case Open: open_dialog(*a); break;
        case Paste: paste_image(*a); break;
        case Example: load_example(*a); break;
        case ImageView: a->showImage = !a->showImage; SetWindowTextW(GetDlgItem(hwnd, ImageView), a->showImage ? L"盤面を表示" : L"元画像を表示"); InvalidateRect(hwnd, nullptr, FALSE); break;
        case Calibrate: calibration_dialog(*a); break;
        case Edit: a->editMode = !a->editMode; if (a->editMode) { a->showImage = false; set_step(*a, 0); stop_play(*a); SetWindowTextW(GetDlgItem(hwnd, ImageView), L"元画像を表示"); }
            SetWindowTextW(GetDlgItem(hwnd, Edit), a->editMode ? L"修正を終了" : L"盤面を修正"); update_enabled(*a); InvalidateRect(hwnd, nullptr, FALSE); break;
        case Start: start_solver(*a); break;
        case Stop: a->cancel.store(true); a->detail = L"探索を停止しています。"; InvalidateRect(hwnd, nullptr, FALSE); break;
        case Reset: stop_play(*a); set_step(*a, 0); break;
        case Previous: stop_play(*a); set_step(*a, a->step - 1); break;
        case Next: stop_play(*a); set_step(*a, a->step + 1); break;
        case Play: if (a->playing) stop_play(*a); else if (!a->moves.empty()) { if (a->step == static_cast<int>(a->moves.size())) set_step(*a, 0); a->playing = true; SetWindowTextW(GetDlgItem(hwnd, Play), L"再生を停止"); SetTimer(hwnd, kPlayTimer, 650, nullptr); } break;
        case RouteList: if (HIWORD(wp) == LBN_SELCHANGE) { stop_play(*a); int index = static_cast<int>(SendMessageW(a->route, LB_GETCURSEL, 0, 0)); if (index >= 0) set_step(*a, index); } break;
        case Export: export_route(*a); break;
        case SaveBoard: save_board(*a); break;
        case ImportBoard: import_board(*a); break;
        } return 0;
    }
    case WM_LBUTTONDOWN: if (a->editMode && !a->running) edit_cell(*a, hit_cell(*a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)), a->selectedColor); return 0;
    case WM_RBUTTONUP: {
        if (!a->editMode || a->running) return 0;
        const int cell = hit_cell(*a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); if (cell < 0) return 0;
        HMENU menu = CreatePopupMenu(); for (int c = 0; c <= 10; ++c) AppendMenuW(menu, MF_STRING, 1000 + c, kColorNames[c]);
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ClientToScreen(hwnd, &p);
        int color = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr); DestroyMenu(menu);
        if (color >= 1000 && color <= 1010) edit_cell(*a, cell, color - 1000); return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wp); wchar_t file[32768]{};
        if (!a->running && DragQueryFileW(drop, 0, file, 32768)) open_path(*a, file); DragFinish(drop); return 0;
    }
    case kWorkerDone: consume_worker(*a); return 0;
    case WM_TIMER:
        if (wp == kUiTimer) { consume_worker(*a); if (a->running) InvalidateRect(hwnd, &a->sideCard, FALSE); }
        else if (wp == kPlayTimer) { set_step(*a, a->step + 1); if (a->step == static_cast<int>(a->moves.size())) stop_play(*a); }
        else if (wp == kSmokeTimer) {
            if (a->smokeSolve >= 0 && a->running) return 0;
            a->smokeExitCode = a->haveBoard && capture_client(*a) ? 0 : 2; DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE: a->cancel.store(true); if (a->worker.joinable()) a->worker.join(); DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        a->cancel.store(true); if (a->worker.joinable()) a->worker.join(); KillTimer(hwnd, kUiTimer); KillTimer(hwnd, kPlayTimer);
        for (HFONT f : {a->font, a->fontSmall, a->fontTitle, a->fontSection, a->fontNumber}) if (f) DeleteObject(f);
        PostQuitMessage(a->smokeExitCode); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    // Dynamic lookup keeps startup compatible with supported older Windows 10 builds.
    auto user32 = GetModuleHandleW(L"user32.dll");
    using DpiFn = BOOL (WINAPI*)(HANDLE);
    auto setDpi = reinterpret_cast<DpiFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setDpi) setDpi(reinterpret_cast<HANDLE>(-4)); else SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = window_proc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(2));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr; RegisterClassExW(&wc);
    WNDCLASSEXW cw = wc; cw.lpfnWndProc = calibration_proc; cw.lpszClassName = kCalibrationClass;
    cw.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassExW(&cw);
    App app;
    HDC initialDc = GetDC(nullptr); app.scale = GetDeviceCaps(initialDc, LOGPIXELSX) / 96.0; ReleaseDC(nullptr, initialDc);
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int initialW = std::min(px(app, 1200), static_cast<int>(work.right - work.left));
    const int initialH = std::min(px(app, 840), static_cast<int>(work.bottom - work.top));
    HWND hwnd = CreateWindowExW(0, kClassName, L"ColorTile Solver", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, initialW, initialH, nullptr, nullptr, instance, &app);
    if (!hwnd) { CoUninitialize(); return 1; }
    ShowWindow(hwnd, show); UpdateWindow(hwnd);
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--smoke-test") app.smoke = true;
            else if (std::wstring(argv[i]) == L"--capture" && i + 1 < argc) app.capturePath = argv[++i];
            else if (std::wstring(argv[i]) == L"--smoke-solve" && i + 1 < argc) { app.smoke = true; app.smokeSolve = wcstod(argv[++i], nullptr); }
            else {
                const std::wstring ext = std::filesystem::path(argv[i]).extension().wstring();
                if (ext == L".ctb" || ext == L".txt") import_board_path(app, argv[i]);
                else open_path(app, argv[i]);
            }
        }
        LocalFree(argv);
    }
    if (app.smoke) {
        if (!app.haveBoard && app.image.rgb.empty()) load_example(app);
        if (app.smokeSolve >= 0) {
            if (!std::isfinite(app.smokeSolve) || app.smokeSolve < 0.1 || app.smokeSolve > 86400) { DestroyWindow(hwnd); CoUninitialize(); return 2; }
            SetWindowTextW(app.limit, budget_string(app.smokeSolve).c_str());
            SendMessageW(app.mode, CB_SETCURSEL, 0, 0); start_solver(app);
        }
        SetTimer(hwnd, kSmokeTimer, app.smokeSolve >= 0 ? 250 : 1500, nullptr);
    }
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        bool handled = false;
        if (msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (msg.wParam == 'V') { SendMessageW(hwnd, WM_COMMAND, Paste, 0); handled = true; }
            else if (msg.wParam == 'O') { SendMessageW(hwnd, WM_COMMAND, Open, 0); handled = true; }
        }
        if (!handled && !IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    CoUninitialize(); return static_cast<int>(msg.wParam);
}
