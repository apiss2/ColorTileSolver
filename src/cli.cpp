#include "core.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
std::string escaped(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (c == '\\' || c == '"') { out += '\\'; out += char(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c >= 32) out += char(c);
    }
    return out;
}
const char* status_name(ct::SolveStatus status) {
    switch (status) {
    case ct::SolveStatus::Optimal: return "optimal";
    case ct::SolveStatus::Feasible: return "feasible";
    case ct::SolveStatus::Partial: return "partial";
    case ct::SolveStatus::Unsolvable: return "unsolvable";
    case ct::SolveStatus::Cancelled: return "cancelled";
    default: return "invalid";
    }
}
int run(const std::vector<std::string>& args) {
    if (args.size() < 2 || args[1] == "--help") {
        std::cout << "ColorTile Solver CLI 1.0\n"
                     "Usage: ColorTileSolverCLI.exe BOARD.ctb [--seconds 30] [--threads 4] [--output result.json]\n"
                     "CTB format: width height, then rows of integer colors (0..10).\n"
                     "seconds=0: unlimited exact optimization. Coordinates in JSON are one-based.\n";
        return args.size() < 2 ? 2 : 0;
    }
    ct::SolveOptions options;
    options.threads = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
    std::string output;
    try {
        for (size_t i = 2; i < args.size(); ++i) {
            if (i + 1 >= args.size()) throw std::runtime_error("Missing option value");
            if (args[i] == "--seconds") options.time_limit_seconds = std::stod(args[++i]);
            else if (args[i] == "--threads") options.threads = std::stoi(args[++i]);
            else if (args[i] == "--output") output = args[++i];
            else throw std::runtime_error("Unknown option: " + args[i]);
        }
        std::ifstream input(std::filesystem::u8path(args[1]));
        ct::Board board;
        if (!(input >> board.width >> board.height) || board.width < 1 || board.height < 1 || board.width > 32 || board.height > 32)
            throw std::runtime_error("Invalid board dimensions (1..32)");
        board.cells.assign(board.width * board.height, 0);
        for (auto& color : board.cells) {
            int value;
            if (!(input >> value) || value < 0 || value > 10) throw std::runtime_error("Invalid or missing cell (0..10)");
            color = static_cast<uint8_t>(value);
        }
        std::string extra;
        if (input >> extra) throw std::runtime_error("Unexpected cells after board");
        std::string validation_error;
        if (!ct::validate_board(board, &validation_error)) throw std::runtime_error(validation_error);
        std::atomic<bool> cancel{false};
        auto result = ct::solve(board, options, cancel);
        if (result.status == ct::SolveStatus::Invalid) throw std::runtime_error(result.message);
        ct::Board replay = board;
        for (const auto& move : result.moves) {
            ct::Move actual;
            if (!ct::apply_move(replay, move.cell, &actual)) throw std::runtime_error("Internal error: invalid result route");
            auto expected = move.removed;
            std::sort(expected.begin(), expected.end());
            std::sort(actual.removed.begin(), actual.removed.end());
            if (expected != actual.removed) throw std::runtime_error("Internal error: incorrect removal set");
        }
        int remaining = std::count_if(replay.cells.begin(), replay.cells.end(), [](uint8_t x) { return x != 0; });
        if (remaining != result.remaining) throw std::runtime_error("Internal error: incorrect remaining count");
        std::ostringstream json;
        json << std::setprecision(9) << "{\n  \"status\": \"" << status_name(result.status)
             << "\",\n  \"remaining\": " << remaining << ",\n  \"clicks\": " << result.moves.size()
             << ",\n  \"lower_bound\": " << result.lower_bound << ",\n  \"seconds\": " << result.seconds
             << ",\n  \"nodes\": " << result.nodes << ",\n  \"message\": \"" << escaped(result.message)
             << "\",\n  \"moves\": [";
        for (size_t i = 0; i < result.moves.size(); ++i) {
            auto& m = result.moves[i];
            if (i) json << ',';
            json << "\n    {\"step\": " << i + 1 << ", \"row\": " << m.cell / board.width + 1
                 << ", \"column\": " << m.cell % board.width + 1 << ", \"removed\": [";
            for (size_t j = 0; j < m.removed.size(); ++j) {
                if (j) json << ',';
                int p = m.removed[j];
                json << "[" << p / board.width + 1 << ',' << p % board.width + 1 << "]";
            }
            json << "]}";
        }
        json << "\n  ]\n}\n";
        if (output.empty()) std::cout << json.str();
        else {
            std::ofstream out(std::filesystem::u8path(output), std::ios::binary);
            if (!out || !(out << json.str())) throw std::runtime_error("Cannot write output file");
        }
        return result.status == ct::SolveStatus::Invalid ? 2 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), n, nullptr, nullptr);
        s.pop_back(); args.push_back(s);
    }
    return run(args);
}
#else
int main(int argc, char** argv) { return run(std::vector<std::string>(argv, argv + argc)); }
#endif
