// ColorTile Solver - independent C++17 implementation, MIT license.
#include "core.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <unordered_set>

namespace ct {
namespace {
using Clock = std::chrono::steady_clock;
constexpr int MaxCells = 1024;
constexpr int MaxTiles = 256;
struct State {
    std::array<uint64_t, 4> words{};
    bool operator==(const State& other) const { return words == other.words; }
    bool has(int id) const { return (words[id >> 6] >> (id & 63)) & 1ULL; }
    void add(int id) { words[id >> 6] |= 1ULL << (id & 63); }
    void remove(int id) { words[id >> 6] &= ~(1ULL << (id & 63)); }
    bool includes(const State& other) const {
        for (int i = 0; i < 4; ++i)
            if ((words[i] & other.words[i]) != other.words[i]) return false;
        return true;
    }
};
uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
int population(uint64_t x) { return __builtin_popcountll(x); }
struct Candidate {
    int cell = -1;
    int count = 0;
    std::array<uint8_t, 4> ids{};
    uint64_t key = 0;
    int priority = 0;
};
struct Model {
    Board board;
    int n = 0;
    int area = 0;
    std::array<int16_t, MaxCells> id_at{};
    std::array<int, MaxTiles> cell_of{};
    std::array<uint8_t, MaxTiles> color{};
    std::array<State, 11> color_mask{};
    std::array<std::vector<State>, 11> triples;
    State initial;
    bool preprocessing_interrupted = false;
    explicit Model(const Board& b, const std::function<bool()>& stop = {}) : board(b), area(b.width * b.height) {
        id_at.fill(-1);
        for (int p = 0; p < area; ++p) if (b.cells[p]) {
            id_at[p] = static_cast<int16_t>(n);
            cell_of[n] = p;
            color[n] = b.cells[p];
            color_mask[color[n]].add(n);
            initial.add(n++);
        }
        // A future odd-color clear needs a three-tile removal. Ignore blockers
        // only for this NECESSARY geometric condition; never solve a modified
        // one-color board and assume it preserves all original solutions.
        uint64_t triple_checks = 0;
        for (int c = 1; c <= kColors; ++c) {
            if (stop && stop()) { preprocessing_interrupted = true; return; }
            std::vector<int> ids;
            for (int id = 0; id < n; ++id) if (color[id] == c) ids.push_back(id);
            for (size_t a = 0; a < ids.size(); ++a)
                for (size_t b1 = a + 1; b1 < ids.size(); ++b1)
                    for (size_t d = b1 + 1; d < ids.size(); ++d) {
                        if ((++triple_checks & 1023) == 0 && stop && stop()) { preprocessing_interrupted = true; return; }
                        int is[3] = {ids[a], ids[b1], ids[d]};
                        bool possible = false;
                        for (int q = 0; q < 3; ++q) {
                            int p0 = cell_of[is[(q + 1) % 3]], p1 = cell_of[is[(q + 2) % 3]];
                            int p2 = cell_of[is[q]];
                            int r0 = p0 / board.width, r1 = p1 / board.width, r2 = p2 / board.width;
                            int c0 = p0 % board.width, c1 = p1 % board.width, c2 = p2 % board.width;
                            if ((c0 == c1 && c2 != c0 && std::min(r0, r1) < r2 && r2 < std::max(r0, r1)) ||
                                (r0 == r1 && r2 != r0 && std::min(c0, c1) < c2 && c2 < std::max(c0, c1)))
                                possible = true;
                        }
                        if (possible) {
                            State mask;
                            for (int id : is) mask.add(id);
                            triples[c].push_back(mask);
                        }
                    }
        }
    }
    int remaining(const State& s) const {
        return population(s.words[0]) + population(s.words[1]) + population(s.words[2]) + population(s.words[3]);
    }
    std::array<int, 11> counts(const State& s) const {
        std::array<int, 11> result{};
        for (int c = 1; c <= kColors; ++c)
            for (int j = 0; j < 4; ++j) result[c] += population(s.words[j] & color_mask[c].words[j]);
        return result;
    }
    bool impossible(const State& s, const std::array<int, 11>& cnt) const {
        for (int c = 1; c <= kColors; ++c) {
            if (cnt[c] == 1) return true;
            if (cnt[c] == 2) {
                int a = -1, b = -1;
                for (int j = 0; j < 4; ++j) {
                    uint64_t bits = s.words[j] & color_mask[c].words[j];
                    while (bits) {
                        int id = j * 64 + __builtin_ctzll(bits);
                        if (a < 0) a = cell_of[id]; else b = cell_of[id];
                        bits &= bits - 1;
                    }
                }
                int dr = std::abs(a / board.width - b / board.width);
                int dc = std::abs(a % board.width - b % board.width);
                if (dr + dc == 1) return true;
            }
            if (cnt[c] & 1) {
                bool possible = false;
                for (const State& t : triples[c]) if (s.includes(t)) { possible = true; break; }
                if (!possible) return true;
            }
        }
        return false;
    }
    int lower_bound(const State& s, const std::array<int, 11>& cnt, int left) const {
        (void)s;
        // Each click removes at most four tiles. Also, each odd color requires
        // its own 3-tile click (two such colors cannot share one click).
        int odds = 0;
        for (int c = 1; c <= kColors; ++c) odds += cnt[c] & 1;
        return std::max((left + 3) / 4, odds + (std::max(0, left - 3 * odds) + 3) / 4);
    }
    int geometric_bound(const State& s) const {
        // A relaxation of all FUTURE clicks: ignore every blocker, and permit
        // the center to become empty later. For a tile t, let g(t) be the
        // largest geometrically possible removal group (2, 3 or 4) containing
        // it. Every actual k-tile move has g(t)>=k for all its tiles, so
        // sum(1/g(t)) <= 1 for that move. The sum over surviving tiles is
        // therefore an admissible lower bound on the remaining click count.
        std::array<uint16_t, MaxCells> up{}, down{}, left{}, right{};
        std::array<std::array<uint16_t, MaxCells>, 4> allow3{}, allow4{};
        std::array<uint8_t, MaxCells> alive_color{};
        std::array<uint8_t, MaxTiles> max_group{};
        const int w = board.width, h = board.height;
        for (int p = 0; p < area; ++p) {
            int id = id_at[p];
            if (id >= 0 && s.has(id)) { alive_color[p] = color[id]; max_group[id] = 2; }
        }
        for (int r = 0; r < h; ++r) {
            uint16_t bits = 0;
            for (int c = 0; c < w; ++c) { int p = r*w+c; left[p] = bits; if (alive_color[p]) bits |= uint16_t(1u << alive_color[p]); }
            bits = 0;
            for (int c = w-1; c >= 0; --c) { int p = r*w+c; right[p] = bits; if (alive_color[p]) bits |= uint16_t(1u << alive_color[p]); }
        }
        for (int c = 0; c < w; ++c) {
            uint16_t bits = 0;
            for (int r = 0; r < h; ++r) { int p = r*w+c; up[p] = bits; if (alive_color[p]) bits |= uint16_t(1u << alive_color[p]); }
            bits = 0;
            for (int r = h-1; r >= 0; --r) { int p = r*w+c; down[p] = bits; if (alive_color[p]) bits |= uint16_t(1u << alive_color[p]); }
        }
        for (int p = 0; p < area; ++p) {
            uint16_t rays[4] = {up[p], down[p], left[p], right[p]};
            for (int d = 0; d < 4; ++d) {
                int others[3], k = 0;
                for (int e = 0; e < 4; ++e) if (e != d) others[k++] = e;
                uint16_t a = rays[others[0]], b = rays[others[1]], c = rays[others[2]];
                allow3[d][p] = rays[d] & ((a & b) | (a & c) | (b & c));
                allow4[d][p] = rays[d] & (((b & c) ? a : 0) | ((a & c) ? b : 0) | ((a & b) ? c : 0));
            }
        }
        auto mark = [&](int p, uint16_t a3, uint16_t a4) {
            if (!alive_color[p]) return;
            int id = id_at[p];
            uint16_t bit = uint16_t(1u << alive_color[p]);
            if (a4 & bit) max_group[id] = 4;
            else if ((a3 & bit) && max_group[id] < 3) max_group[id] = 3;
        };
        for (int r = 0; r < h; ++r) {
            uint16_t a3 = 0, a4 = 0;
            for (int c = 0; c < w; ++c) { int p=r*w+c; mark(p,a3,a4); a3 |= allow3[3][p]; a4 |= allow4[3][p]; }
            a3 = a4 = 0;
            for (int c = w-1; c >= 0; --c) { int p=r*w+c; mark(p,a3,a4); a3 |= allow3[2][p]; a4 |= allow4[2][p]; }
        }
        for (int c = 0; c < w; ++c) {
            uint16_t a3 = 0, a4 = 0;
            for (int r = 0; r < h; ++r) { int p=r*w+c; mark(p,a3,a4); a3 |= allow3[1][p]; a4 |= allow4[1][p]; }
            a3 = a4 = 0;
            for (int r = h-1; r >= 0; --r) { int p=r*w+c; mark(p,a3,a4); a3 |= allow3[0][p]; a4 |= allow4[0][p]; }
        }
        int twelfths = 0;
        for (int id = 0; id < n; ++id) if (max_group[id]) twelfths += 12 / max_group[id];
        return (twelfths + 11) / 12;
    }
    std::vector<Candidate> moves(const State& s) const {
        // Four linear sweeps: the first occupied tile on every cardinal ray.
        std::array<int16_t, MaxCells> alive{}, up{}, down{}, left{}, right{};
        for (int p = 0; p < area; ++p) {
            int id = id_at[p];
            alive[p] = static_cast<int16_t>(id >= 0 && s.has(id) ? id : -1);
        }
        const int w = board.width, h = board.height;
        for (int r = 0; r < h; ++r) {
            int16_t last = -1;
            for (int c = 0; c < w; ++c) { int p = r * w + c; left[p] = last; if (alive[p] >= 0) last = alive[p]; }
            last = -1;
            for (int c = w - 1; c >= 0; --c) { int p = r * w + c; right[p] = last; if (alive[p] >= 0) last = alive[p]; }
        }
        for (int c = 0; c < w; ++c) {
            int16_t last = -1;
            for (int r = 0; r < h; ++r) { int p = r * w + c; up[p] = last; if (alive[p] >= 0) last = alive[p]; }
            last = -1;
            for (int r = h - 1; r >= 0; --r) { int p = r * w + c; down[p] = last; if (alive[p] >= 0) last = alive[p]; }
        }
        std::vector<Candidate> result;
        result.reserve(128);
        std::array<uint64_t, 2048> unique{};
        for (int p = 0; p < area; ++p) if (alive[p] < 0) {
            int ids[4] = {up[p], down[p], left[p], right[p]};
            Candidate m;
            m.cell = p;
            for (int d = 0; d < 4; ++d) if (ids[d] >= 0) {
                bool paired = false;
                for (int e = 0; e < 4; ++e)
                    if (e != d && ids[e] >= 0 && color[ids[d]] == color[ids[e]]) paired = true;
                if (paired) m.ids[m.count++] = static_cast<uint8_t>(ids[d]);
            }
            if (m.count < 2) continue;
            std::sort(m.ids.begin(), m.ids.begin() + m.count);
            for (int i = 0; i < m.count; ++i) m.key |= (uint64_t(m.ids[i]) + 1) << (9 * i);
            size_t bucket = mix(m.key) & (unique.size() - 1);
            while (unique[bucket] && unique[bucket] != m.key) bucket = (bucket + 1) & (unique.size() - 1);
            if (unique[bucket]) continue;
            unique[bucket] = m.key;
            result.push_back(m);
        }
        return result;
    }
    Move external(const Candidate& m) const {
        Move result;
        result.cell = m.cell;
        for (int k = 0; k < m.count; ++k) result.removed.push_back(cell_of[m.ids[k]]);
        std::sort(result.removed.begin(), result.removed.end());
        return result;
    }
};
struct Cache {
    struct Entry { State key; uint32_t generation = 0; uint16_t depth = 0; };
    std::vector<Entry> entries;
    uint32_t generation = 1;
    explicit Cache(size_t requested) {
        size_t capacity = 1;
        while (capacity <= requested / 2) capacity *= 2;
        entries.resize(capacity);
    }
    void reset() {
        if (++generation == 0) { for (auto& e : entries) e.generation = 0; generation = 1; }
    }
    bool dominated(const State& key, int depth) {
        uint64_t h = mix(key.words[0]) ^ mix(key.words[1] + 23) ^ mix(key.words[2] + 47) ^ mix(key.words[3] + 73);
        Entry& e = entries[h & (entries.size() - 1)];
        if (e.generation == generation && e.key == key && e.depth <= depth) return true;
        e.key = key;
        e.generation = generation;
        e.depth = static_cast<uint16_t>(depth);
        return false;
    }
};
struct Shared {
    const Model& model;
    const SolveOptions& options;
    std::atomic<bool>& cancel;
    ProgressCallback callback;
    Clock::time_point started = Clock::now();
    std::atomic<bool> done{false};
    std::atomic<int> upper{1000000};
    std::atomic<int> best_left;
    std::atomic<uint64_t> nodes{0};
    std::mutex mutex;
    std::vector<Move> best_path;
    int root_lower = 0;
    std::string phase = "finding_solution";
    Clock::time_point last_report = started;
    Shared(const Model& m, const SolveOptions& o, std::atomic<bool>& c, ProgressCallback p, Clock::time_point start)
        : model(m), options(o), cancel(c), callback(std::move(p)), started(start), best_left(m.n) {
        root_lower = std::max(m.lower_bound(m.initial, m.counts(m.initial), m.n), m.geometric_bound(m.initial));
    }
    double seconds() const { return std::chrono::duration<double>(Clock::now() - started).count(); }
    bool stop() const {
        return done.load(std::memory_order_relaxed) || cancel.load(std::memory_order_relaxed) ||
            (options.time_limit_seconds > 0 && seconds() >= options.time_limit_seconds);
    }
    void report(bool force = false) {
        if (!callback) return;
        std::lock_guard<std::mutex> lock(mutex);
        auto now = Clock::now();
        if (!force && now - last_report < std::chrono::milliseconds(100)) return;
        last_report = now;
        SolveProgress p;
        p.nodes = nodes.load(std::memory_order_relaxed);
        p.elapsed = seconds();
        p.best_remaining = best_left.load(std::memory_order_relaxed);
        p.best_moves = static_cast<int>(best_path.size());
        p.lower_bound = root_lower;
        p.phase = phase;
        p.moves = best_path;
        callback(p);
    }
    void improve(const std::vector<Candidate>& path, int left) {
        int current_left = best_left.load(std::memory_order_relaxed);
        int current_upper = upper.load(std::memory_order_relaxed);
        if (left > current_left || (left == 0 && int(path.size()) >= current_upper)) return;
        if (left == current_left && left != 0) return;
        std::lock_guard<std::mutex> lock(mutex);
        current_left = best_left.load(std::memory_order_relaxed);
        current_upper = upper.load(std::memory_order_relaxed);
        if (left > current_left || (left == 0 && int(path.size()) >= current_upper) ||
            (left == current_left && left != 0)) return;
        best_path.clear();
        for (const auto& m : path) best_path.push_back(model.external(m));
        best_left.store(left, std::memory_order_relaxed);
        if (left == 0) upper.store(static_cast<int>(path.size()), std::memory_order_relaxed);
    }
};
struct Worker {
    Shared& shared;
    const Model& model;
    Cache cache;
    std::vector<Candidate> path;
    uint64_t rng;
    uint64_t local_nodes = 0;
    uint64_t pending_nodes = 0;
    uint64_t node_limit = 0;
    bool complete = true;
    bool randomized = false;
    double deadline = 0;
    Worker(Shared& s, size_t capacity, uint64_t seed) : shared(s), model(s.model), cache(capacity), rng(seed) {
        path.reserve(128);
    }
    ~Worker() { flush(); }
    void flush() { shared.nodes.fetch_add(pending_nodes, std::memory_order_relaxed); pending_nodes = 0; }
    uint64_t random() { rng += 0x9e3779b97f4a7c15ULL; return mix(rng); }
    bool stopped() {
        if ((node_limit && local_nodes >= node_limit) || (deadline > 0 && shared.seconds() >= deadline) || shared.stop()) {
            complete = false;
            return true;
        }
        return false;
    }
    void order(std::vector<Candidate>& moves, const std::array<int, 11>& counts) {
        std::array<int, MaxTiles> degree{};
        for (const auto& m : moves) for (int i = 0; i < m.count; ++i) ++degree[m.ids[i]];
        for (auto& m : moves) {
            int score = m.count * 1200;
            std::array<int, 11> removed{};
            for (int i = 0; i < m.count; ++i) {
                int id = m.ids[i];
                score += 600 / (degree[id] + 1);
                ++removed[model.color[id]];
            }
            for (int c = 1; c <= kColors; ++c) if (removed[c]) {
                if (counts[c] == removed[c]) score += 500;
                if (counts[c] - removed[c] == 1) score -= 100000;
                if (removed[c] == 3 && !(counts[c] & 1)) score -= 1100;
                if (removed[c] == 3 && (counts[c] & 1)) score += 350;
            }
            if (randomized) score += int(random() % 1800);
            m.priority = score;
        }
        std::stable_sort(moves.begin(), moves.end(), [](const Candidate& a, const Candidate& b) { return a.priority > b.priority; });
    }
    void dfs(const State& state, int left, const std::array<int, 11>& cnt) {
        ++local_nodes; ++pending_nodes;
        if ((local_nodes & 127) == 0) {
            flush();
            shared.report();
            if (stopped()) return;
        }
        shared.improve(path, left);
        if (!left) return;
        const int upper = shared.upper.load(std::memory_order_relaxed);
        const int lower = model.lower_bound(state, cnt, left);
        if (int(path.size()) + lower >= upper) return;
        if (model.impossible(state, cnt)) return;
        if (!randomized && left <= 48 && int(path.size()) + lower >= upper - 8 &&
            int(path.size()) + model.geometric_bound(state) >= upper) return;
        if (cache.dominated(state, static_cast<int>(path.size()))) return;
        auto moves = model.moves(state);
        order(moves, cnt);
        for (const auto& m : moves) {
            if (!complete) return;
            if ((node_limit && local_nodes >= node_limit) || shared.stop()) { complete = false; return; }
            State child = state;
            auto child_counts = cnt;
            for (int i = 0; i < m.count; ++i) { child.remove(m.ids[i]); --child_counts[model.color[m.ids[i]]]; }
            path.push_back(m);
            dfs(child, left - m.count, child_counts);
            path.pop_back();
            if (shared.upper.load(std::memory_order_relaxed) == shared.root_lower) return;
        }
    }
    void attempt(bool random_order, uint64_t budget, double until) {
        cache.reset(); path.clear(); local_nodes = 0; node_limit = budget; deadline = until;
        complete = true; randomized = random_order;
        dfs(model.initial, model.n, model.counts(model.initial));
        flush();
    }
};
} // namespace

bool validate_board(const Board& board, std::string* error) {
    auto fail = [&](const char* message) { if (error) *error = message; return false; };
    if (board.width < 1 || board.height < 1 || board.width > 32 || board.height > 32)
        return fail("Board dimensions must be between 1 and 32.");
    if (board.cells.size() != static_cast<size_t>(board.width * board.height)) return fail("Board cell count does not match dimensions.");
    int n = 0;
    for (uint8_t c : board.cells) { if (c > kColors) return fail("Unknown tile color."); if (c) ++n; }
    if (n > MaxTiles) return fail("At most 256 tiles are supported.");
    return true;
}
std::vector<Move> legal_moves(const Board& board) {
    if (!validate_board(board)) return {};
    // Public routine retains all clickable positions, including equivalent
    // next states, so the GUI can validate any user-selected legal click.
    std::vector<Move> moves;
    const int w = board.width, h = board.height;
    constexpr int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
    for (int p = 0; p < w * h; ++p) if (!board.cells[p]) {
        int seen[4] = {-1, -1, -1, -1};
        std::array<int, 11> counts{};
        for (int d = 0; d < 4; ++d) {
            int r = p / w + dr[d], c = p % w + dc[d];
            while (r >= 0 && r < h && c >= 0 && c < w) {
                int q = r * w + c;
                if (board.cells[q]) { seen[d] = q; ++counts[board.cells[q]]; break; }
                r += dr[d]; c += dc[d];
            }
        }
        Move m; m.cell = p;
        for (int q : seen) if (q >= 0 && counts[board.cells[q]] >= 2) m.removed.push_back(q);
        if (!m.removed.empty()) { std::sort(m.removed.begin(), m.removed.end()); moves.push_back(std::move(m)); }
    }
    return moves;
}
bool apply_move(Board& board, int cell, Move* applied) {
    if (!validate_board(board) || cell < 0 || cell >= int(board.cells.size()) || board.cells[cell]) return false;
    for (const auto& m : legal_moves(board)) if (m.cell == cell) {
        for (int q : m.removed) board.cells[q] = 0;
        if (applied) *applied = m;
        return true;
    }
    return false;
}
SolveResult solve(const Board& board, const SolveOptions& options, std::atomic<bool>& cancel, ProgressCallback progress) {
    const auto started = Clock::now();
    SolveResult result;
    if (!validate_board(board, &result.message)) return result;
    if (!std::isfinite(options.time_limit_seconds)) { result.message = "Time limit must be finite."; return result; }
    auto deadline_reached = [&] {
        return cancel.load(std::memory_order_relaxed) || (options.time_limit_seconds > 0 &&
            std::chrono::duration<double>(Clock::now() - started).count() >= options.time_limit_seconds);
    };
    Model model(board, deadline_reached);
    if (model.preprocessing_interrupted) {
        result.status = cancel.load(std::memory_order_relaxed) ? SolveStatus::Cancelled : SolveStatus::Partial;
        result.remaining = model.n;
        result.lower_bound = (model.n + 3) / 4;
        result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
        result.message = "Search stopped during preprocessing; no impossibility or optimality claim is made.";
        return result;
    }
    Shared shared(model, options, cancel, std::move(progress), started);
    result.remaining = model.n;
    if (!model.n) { result.status = SolveStatus::Optimal; result.remaining = 0; result.message = "Already clear; minimum 0 clicks."; return result; }
    const unsigned available = std::max(1u, std::thread::hardware_concurrency());
    const int threads = std::max(1, std::min({options.threads, 16, int(available)}));
    size_t total_capacity = std::max<size_t>(1024, std::min<size_t>(options.max_table_entries, 4000000));
    if (options.time_limit_seconds > 0 && options.time_limit_seconds < 1)
        total_capacity = std::min(total_capacity, std::max<size_t>(1024, static_cast<size_t>(options.time_limit_seconds * 100000)));
    const size_t capacity = std::max<size_t>(64, total_capacity / threads);
    Worker main_worker(shared, capacity, options.seed);
    bool exhaustive = false;
    // Helper threads search differently ordered short subtrees, improving the
    // incumbent. The main worker alone performs the complete proof search.
    std::vector<std::thread> helpers;
    for (int t = 1; t < threads; ++t) helpers.emplace_back([&, t] {
        Worker worker(shared, capacity, options.seed + uint64_t(t) * 1013904223ULL);
        while (!shared.stop() && shared.upper.load(std::memory_order_relaxed) > shared.root_lower)
            worker.attempt(true, 2500 + (worker.random() % 5000), 0);
    });
    const double warmup = options.time_limit_seconds > 0 ? std::min(2.0, options.time_limit_seconds * 0.18) : 2.0;
    bool first = true;
    while (!shared.stop() && shared.upper.load(std::memory_order_relaxed) > shared.root_lower && shared.seconds() < warmup) {
        main_worker.attempt(!first, first ? 12000 : 4000 + main_worker.random() % 4000, warmup);
        first = false;
        if (!options.prove_optimal && shared.best_left.load(std::memory_order_relaxed) == 0) break;
        if (main_worker.complete) { exhaustive = true; break; }
    }
    if (!shared.stop() && !exhaustive && shared.upper.load(std::memory_order_relaxed) > shared.root_lower) {
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.phase = options.prove_optimal ? "proving_shortest" : "finding_solution";
        }
        shared.report(true);
        if (options.prove_optimal) {
            main_worker.attempt(false, 0, 0);
            exhaustive = main_worker.complete;
        } else {
            while (!shared.stop() && shared.best_left.load(std::memory_order_relaxed) != 0) {
                main_worker.attempt(true, 8000, 0);
                if (main_worker.complete) { exhaustive = true; break; }
            }
        }
    }
    // Setting done tells helpers to leave even when the requested limit was 0.
    shared.done.store(true, std::memory_order_relaxed);
    for (auto& helper : helpers) helper.join();
    main_worker.flush();
    result.moves = shared.best_path;
    result.remaining = shared.best_left.load(std::memory_order_relaxed);
    result.lower_bound = shared.root_lower;
    result.seconds = shared.seconds();
    result.nodes = shared.nodes.load(std::memory_order_relaxed);
    if (result.remaining == 0 && (exhaustive || int(result.moves.size()) == result.lower_bound)) {
        result.status = SolveStatus::Optimal;
        result.lower_bound = static_cast<int>(result.moves.size());
        result.message = "A shortest full-clear sequence is proven.";
    } else if (exhaustive && result.remaining != 0) {
        result.status = SolveStatus::Unsolvable;
        result.message = "No full-clear sequence exists. A partial route may be shown.";
    } else if (cancel.load(std::memory_order_relaxed)) {
        result.status = SolveStatus::Cancelled;
        result.message = "Search cancelled; the best sequence found is retained.";
    } else if (result.remaining == 0) {
        result.status = SolveStatus::Feasible;
        result.message = "Full-clear sequence found; minimum click count is not yet proven.";
    } else {
        result.status = SolveStatus::Partial;
        result.message = "Time limit reached; the partial sequence is not a proof of impossibility.";
    }
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.root_lower = result.lower_bound;
        shared.phase = "finished";
    }
    shared.report(true);
    return result;
}
} // namespace ct
