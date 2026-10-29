#pragma once
#include "core.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ct {
enum class PlaybackAction { Wait, Click, Advanced, Complete, BoardChanged, NoResponse };

// Only complete, replay-verified routes may drive another application's board.
// The caller supplies screen observations and explicitly acknowledges a sent click.
// No second click is issued until the first move is visible on the actual board.
class VerifiedPlayback {
public:
    bool reset(const Board& initial, const std::vector<Move>& moves,
               bool automatic, int interval_ms, std::string* error = nullptr);
    PlaybackAction observe(const Board* board, bool reliable, uint64_t now_ms);
    bool clicked(uint64_t now_ms);
    void set_interval(int interval_ms);
    size_t step() const { return step_; }
    size_t size() const { return moves_.size(); }
    int next_cell() const;
    bool awaiting_response() const { return awaiting_; }
    const Board& expected() const { return boards_[step_]; }
private:
    std::vector<Board> boards_;
    std::vector<Move> moves_;
    size_t step_ = 0;
    int interval_ = 600;
    bool automatic_ = false, awaiting_ = false, offered_ = false;
    bool have_stable_ = false, have_mismatch_ = false;
    int stable_kind_ = -1;
    uint64_t stable_since_ = 0, mismatch_since_ = 0;
    uint64_t clicked_at_ = 0, next_click_at_ = 0;
    PlaybackAction terminal_ = PlaybackAction::Wait;
};
} // namespace ct
