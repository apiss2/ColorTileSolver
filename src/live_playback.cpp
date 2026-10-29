#include "live_playback.h"
#include <algorithm>

namespace ct {
namespace {
bool same(const Board& a, const Board& b) {
    return a.width == b.width && a.height == b.height && a.cells == b.cells;
}
bool empty(const Board& board) {
    return std::none_of(board.cells.begin(), board.cells.end(), [](uint8_t color) { return color != 0; });
}
} // namespace

bool VerifiedPlayback::reset(const Board& initial, const std::vector<Move>& moves,
                             bool automatic, int interval_ms, std::string* error) {
    *this = VerifiedPlayback{};
    auto fail = [&](const char* message) { if (error) *error = message; return false; };
    if (!validate_board(initial)) return fail("Invalid initial board");
    if (moves.empty() || empty(initial)) return fail("A non-empty complete route is required");
    if (interval_ms < 100 || interval_ms > 60000 || interval_ms % 100 != 0)
        return fail("Interval must be 100 to 60000 milliseconds, in steps of 100");
    Board state = initial;
    std::vector<Board> states{state};
    for (const auto& move : moves) {
        Move actual;
        if (!apply_move(state, move.cell, &actual)) return fail("Route contains an illegal click");
        auto requested = move.removed, removed = actual.removed;
        std::sort(requested.begin(), requested.end()); std::sort(removed.begin(), removed.end());
        if (requested != removed) return fail("Route removal list does not match the board");
        states.push_back(state);
    }
    if (!empty(state)) return fail("Partial routes cannot control the screen");
    boards_ = std::move(states); moves_ = moves; automatic_ = automatic; interval_ = interval_ms;
    return true;
}

void VerifiedPlayback::set_interval(int interval_ms) {
    if (interval_ms < 100 || interval_ms > 60000 || interval_ms % 100 != 0) return;
    interval_ = interval_ms;
    if (awaiting_ || step_ > 0) next_click_at_ = clicked_at_ + static_cast<uint64_t>(interval_);
}

int VerifiedPlayback::next_cell() const {
    return step_ < moves_.size() ? moves_[step_].cell : -1;
}

PlaybackAction VerifiedPlayback::observe(const Board* board, bool reliable, uint64_t now) {
    if (terminal_ != PlaybackAction::Wait) return terminal_;
    if (boards_.empty()) return terminal_ = PlaybackAction::BoardChanged;
    if (step_ == moves_.size()) return terminal_ = PlaybackAction::Complete;
    offered_ = false;
    int kind = -1;
    if (reliable && board) {
        if (same(*board, boards_[step_])) kind = 0;
        else if (same(*board, boards_[step_ + 1])) kind = 1;
    }
    // Confirm a screen state in two separated observations, so a transitional
    // frame cannot immediately authorize an input event.
    if (kind != stable_kind_ || !have_stable_) {
        stable_kind_ = kind; stable_since_ = now; have_stable_ = true;
    }
    const bool stable = kind >= 0 && now >= stable_since_ && now - stable_since_ >= 60;
    if (kind == 1 && (awaiting_ || !automatic_)) {
        have_mismatch_ = false;
        if (!stable) return PlaybackAction::Wait;
        ++step_; awaiting_ = false; have_stable_ = false;
        if (step_ == moves_.size()) return terminal_ = PlaybackAction::Complete;
        return PlaybackAction::Advanced;
    }
    if (awaiting_) {
        // Never resend a click, including when its acknowledgment is delayed.
        if (now >= clicked_at_ && now - clicked_at_ >= 2000)
            return terminal_ = kind == 0 ? PlaybackAction::NoResponse : PlaybackAction::BoardChanged;
        return PlaybackAction::Wait;
    }
    if (kind == 0) {
        have_mismatch_ = false;
        if (automatic_ && stable && now >= next_click_at_) {
            offered_ = true;
            return PlaybackAction::Click;
        }
        return PlaybackAction::Wait;
    }
    if (!have_mismatch_) { have_mismatch_ = true; mismatch_since_ = now; }
    // Brief animation or capture uncertainty is tolerated without producing input.
    if (now >= mismatch_since_ && now - mismatch_since_ >= 500)
        return terminal_ = PlaybackAction::BoardChanged;
    return PlaybackAction::Wait;
}

bool VerifiedPlayback::clicked(uint64_t now) {
    if (!automatic_ || !offered_ || awaiting_ || terminal_ != PlaybackAction::Wait) return false;
    offered_ = false; awaiting_ = true; clicked_at_ = now;
    next_click_at_ = now + static_cast<uint64_t>(interval_); have_stable_ = false;
    return true;
}
} // namespace ct
