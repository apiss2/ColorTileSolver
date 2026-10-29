// Independent shortest-path oracle for small boards.
// Build with src/solver.cpp and C++17; no test framework is required.
#include "core.h"
#include <algorithm>
#include <atomic>
#include <iostream>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Cells = std::vector<uint8_t>;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Implement the game rule independently of ct::legal_moves and its internal
// optimized generator. Each returned integer is the set of removed positions.
std::vector<int> oracle_moves(const Cells& cells, int width, int height) {
    std::vector<int> result;
    for (int p = 0; p < width * height; ++p) {
        if (cells[p]) continue;
        const int row = p / width, col = p % width;
        int seen[4] = {-1, -1, -1, -1};
        for (int r = row - 1; r >= 0; --r) {
            if (cells[r * width + col]) { seen[0] = r * width + col; break; }
        }
        for (int r = row + 1; r < height; ++r) {
            if (cells[r * width + col]) { seen[1] = r * width + col; break; }
        }
        for (int c = col - 1; c >= 0; --c) {
            if (cells[row * width + c]) { seen[2] = row * width + c; break; }
        }
        for (int c = col + 1; c < width; ++c) {
            if (cells[row * width + c]) { seen[3] = row * width + c; break; }
        }
        int removed = 0;
        for (int a : seen) for (int b : seen) {
            if (a >= 0 && b >= 0 && a != b && cells[a] == cells[b])
                removed |= 1 << a;
        }
        if (removed) result.push_back(removed);
    }
    return result;
}

int breadth_first_minimum(const ct::Board& board) {
    const int area = static_cast<int>(board.cells.size());
    require(area <= 16, "The independent oracle supports at most 16 cells.");
    int initial = 0;
    for (int p = 0; p < area; ++p) if (board.cells[p]) initial |= 1 << p;
    std::queue<int> queue;
    std::vector<int> distance(1 << area, -1);
    distance[initial] = 0;
    queue.push(initial);
    while (!queue.empty()) {
        const int state = queue.front();
        queue.pop();
        if (!state) return distance[state];
        Cells cells = board.cells;
        for (int p = 0; p < area; ++p) if (!(state & (1 << p))) cells[p] = 0;
        for (int removed : oracle_moves(cells, board.width, board.height)) {
            const int next = state & ~removed;
            if (distance[next] >= 0) continue;
            distance[next] = distance[state] + 1;
            queue.push(next);
        }
    }
    return -1;
}

void check_against_oracle(const ct::Board& board, const ct::SolveOptions& options) {
    std::atomic<bool> cancel{false};
    const int expected = breadth_first_minimum(board);
    const auto result = ct::solve(board, options, cancel);
    if (expected < 0) {
        require(result.status == ct::SolveStatus::Unsolvable,
                "Solver did not prove a genuinely unsolvable small board.");
    } else {
        require(result.status == ct::SolveStatus::Optimal,
                "Solver did not prove the small-board minimum.");
        require(static_cast<int>(result.moves.size()) == expected,
                "Solver disagrees with independent BFS on minimum clicks.");
    }
    ct::Board replay = board;
    for (const auto& move : result.moves) {
        ct::Move actual;
        require(ct::apply_move(replay, move.cell, &actual), "Returned click is illegal.");
        require(actual.removed == move.removed, "Returned deletion set differs on replay.");
    }
    const int remaining = static_cast<int>(std::count_if(
        replay.cells.begin(), replay.cells.end(), [](uint8_t c) { return c != 0; }));
    require(remaining == result.remaining, "Reported remaining count differs on replay.");
}
} // namespace

int main() {
    try {
        std::mt19937 random(8292);
        ct::SolveOptions options;
        options.time_limit_seconds = 0;
        options.max_table_entries = 64;
        options.threads = 1;
        int checked = 0;
        for (int kind = 0; kind < 3; ++kind) {
            const int trials = kind == 0 ? 19683 : 500;
            for (int trial = 0; trial < trials; ++trial) {
                ct::Board board;
                board.width = kind == 2 ? 4 : 3;
                board.height = kind == 0 ? 3 : 4;
                board.cells.resize(board.width * board.height);
                if (kind == 0) {
                    int value = trial;
                    for (auto& color : board.cells) { color = value % 3; value /= 3; }
                } else {
                    for (auto& color : board.cells) color = random() % 4;
                }
                check_against_oracle(board, options);
                ++checked;
            }
        }
        std::cout << "Independent BFS/full-clear/replay: " << checked << " boards passed.\n";

        // The old three-tile pruning bug: these three tiles clear in one move.
        ct::Board triple;
        triple.width = triple.height = 3;
        triple.cells = {0, 8, 0, 0, 0, 8, 0, 8, 0};
        options.threads = 4;
        check_against_oracle(triple, options);
        std::cout << "Three-tile T and threaded search: passed.\n";

        // Both two-tile color groups disappear in the same center click.
        ct::Board simultaneous = triple;
        simultaneous.cells = {0, 8, 0, 2, 0, 2, 0, 8, 0};
        check_against_oracle(simultaneous, options);
        std::cout << "Two-color simultaneous removal: passed.\n";

        std::atomic<bool> cancel{true};
        ct::Board cancelled = triple;
        cancelled.cells = {1, 0, 1, 0, 0, 0, 0, 0, 0};
        const auto stopped = ct::solve(cancelled, options, cancel);
        require(stopped.status == ct::SolveStatus::Cancelled, "Pre-cancel was not honored.");
        std::cout << "Pre-cancel status: passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
