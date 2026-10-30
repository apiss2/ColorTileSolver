#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "core.h"

// All coordinates are physical screen pixels. Call from the GUI thread, after
// enabling per-monitor DPI awareness. This module never bypasses foreground or
// integrity-level restrictions imposed by Windows.
namespace desktop {
struct Target {
    RECT selection{};
    HWND window = nullptr;
    RECT window_rect{};
    DWORD process_id = 0;
    // Recorded by select_region. A zero rectangle is allowed for test callers.
    RECT virtual_screen{};
};

// Drag a rectangle over a frozen desktop image. A cancelled selection returns
// false with an empty error. The owner's visibility and placement are restored.
// The returned image is a fresh, validated capture after selection is finished.
bool select_region(HWND owner, Target& target, ct::Image& image, std::wstring& error);

// A different foreground window, moved target, covered selection or changed
// display layout fails closed. capture temporarily hides the hint ring.
bool validate(const Target& target, std::wstring& error, bool require_foreground = true);
bool capture(const Target& target, ct::Image& image, std::wstring& error,
             bool require_foreground = true);
bool focus_target(const Target& target, std::wstring& error);

// Sends exactly one click at a checked point inside the selected rectangle.
// The caller must first verify that the current board matches its expected state.
bool click_cell(const Target& target, POINT screen_point, std::wstring& error);

// A nonactivating, click-through ring. step/total are retained for diagnostics;
// the ring is deliberately hollow so that the next click remains easy to see.
bool show_hint(const Target& target, RECT screen_cell, int step, int total);
void hide_hint();
void destroy_hint();
} // namespace desktop
