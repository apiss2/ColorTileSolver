#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ct {
constexpr int kColors = 10;
// 0 empty, 1 gray, 2 blue, 3 cyan, 4 green, 5 yellow,
// 6 brown, 7 orange, 8 red, 9 pink, 10 magenta.
inline constexpr std::array<uint32_t, 11> kPalette = {
    0xF4F6F8, 0xBBBBBB, 0x0066FF, 0x66CCCC, 0x00CC00,
    0xCCCC66, 0xCC6600, 0xFF9900, 0xFF6666, 0xFF88FF, 0xCC66CC
};
struct Board {
    int width = 23;
    int height = 15;
    std::vector<uint8_t> cells = std::vector<uint8_t>(23 * 15, 0);
};
struct Move {
    int cell = -1;
    std::vector<int> removed;
};
struct SolveOptions {
    double time_limit_seconds = 30.0;
    uint32_t max_table_entries = 1000000;
    int threads = 1;
    bool prove_optimal = true;
    uint64_t seed = 20261029;
};
enum class SolveStatus { Optimal, Feasible, Partial, Unsolvable, Cancelled, Invalid };
struct SolveProgress {
    uint64_t nodes = 0;
    double elapsed = 0;
    int best_remaining = 0;
    int best_moves = 0;
    int lower_bound = 0;
    std::string phase;
    std::vector<Move> moves;
};
struct SolveResult {
    SolveStatus status = SolveStatus::Invalid;
    std::vector<Move> moves;
    int remaining = 0;
    int lower_bound = 0;
    double seconds = 0;
    uint64_t nodes = 0;
    std::string message;
};
using ProgressCallback = std::function<void(const SolveProgress&)>;
std::vector<Move> legal_moves(const Board& board);
bool apply_move(Board& board, int cell, Move* applied = nullptr);
bool validate_board(const Board& board, std::string* error = nullptr);
SolveResult solve(const Board& board, const SolveOptions& options,
                  std::atomic<bool>& cancel, ProgressCallback progress = {});

struct Image {
    int width = 0;
    int height = 0;
    // RGB bytes in top-to-bottom order.
    std::vector<uint8_t> rgb;
};
struct GridGeometry {
    double left = 0;
    double top = 0;
    double cell_width = 25;
    double cell_height = 25;
    int columns = 23;
    int rows = 15;
};
struct Recognition {
    bool ok = false;
    Board board;
    GridGeometry grid;
    // One confidence [0,1] per cell. Low-confidence cells are editable in GUI.
    std::vector<float> confidence;
    int uncertain_cells = 0;
    std::string message;
};
Recognition recognize(const Image& image, int columns = 23, int rows = 15);
Recognition sample_grid(const Image& image, const GridGeometry& grid);
} // namespace ct
