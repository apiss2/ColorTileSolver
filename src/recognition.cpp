#include "core.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <utility>

namespace ct {
namespace {
struct Component {
    int x0, y0, x1, y1, count;
    double x() const { return (x0 + x1) * .5; }
    double y() const { return (y0 + y1) * .5; }
    double w() const { return x1 - x0 + 1; }
    double h() const { return y1 - y0 + 1; }
    double side() const { return (w() + h()) * .5; }
};

bool valid_image(const Image& im) {
    return im.width > 0 && im.height > 0 && im.width <= 16384 && im.height <= 16384 &&
           static_cast<uint64_t>(im.width) * im.height <= 64000000 &&
           im.rgb.size() == static_cast<size_t>(im.width) * im.height * 3;
}

double median(std::vector<double> a) {
    if (a.empty()) return 0;
    const size_t mid = a.size() / 2;
    std::nth_element(a.begin(), a.begin() + mid, a.end());
    const double high = a[mid];
    if (a.size() % 2) return high;
    return (high + *std::max_element(a.begin(), a.begin() + mid)) * .5;
}

std::vector<Component> components(const Image& im) {
    const int w = im.width, h = im.height;
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < mask.size(); ++i) {
        const auto* p = &im.rgb[i * 3];
        const int lo = std::min({p[0], p[1], p[2]});
        const int hi = std::max({p[0], p[1], p[2]});
        mask[i] = hi - lo > 35 && hi > 70;
    }
    std::vector<Component> result;
    std::vector<int> pending;
    pending.reserve(4096);
    for (int index = 0; index < w * h; ++index) {
        if (!mask[index]) continue;
        pending.clear();
        pending.push_back(index);
        mask[index] = 0;
        Component c{index % w, index / w, index % w, index / w, 0};
        for (size_t head = 0; head < pending.size(); ++head) {
            const int i = pending[head], x = i % w, y = i / w;
            c.x0 = std::min(c.x0, x); c.x1 = std::max(c.x1, x);
            c.y0 = std::min(c.y0, y); c.y1 = std::max(c.y1, y);
            ++c.count;
            auto push = [&](int p) {
                if (mask[p]) { mask[p] = 0; pending.push_back(p); }
            };
            if (x) push(i - 1);
            if (x + 1 < w) push(i + 1);
            if (y) push(i - w);
            if (y + 1 < h) push(i + w);
        }
        const double aspect = c.w() / c.h();
        if (c.w() >= 4 && c.h() >= 4 && c.w() <= 512 && c.h() <= 512 &&
            aspect >= .72 && aspect <= 1.39 &&
            c.count >= .45 * c.w() * c.h()) {
            result.push_back(c);
        }
    }
    return result;
}

struct AxisFit {
    bool ok = false;
    double center = 0, pitch = 0;
    int distinct = 0;
    int inliers = 0;
};

// Fit repeated square centers to an integer lattice. Least squares compensates
// fractional image scaling; phase fitting rejects similarly sized UI icons.
AxisFit fit_axis(const std::vector<Component>& cs, bool horizontal,
                 int length, double approximate_side, int image_length) {
    AxisFit out;
    if (cs.size() < 4) return out;
    std::vector<double> pitches;
    for (size_t i = 0; i < cs.size(); ++i) {
        for (size_t j = i + 1; j < cs.size(); ++j) {
            const double orth = horizontal ? std::abs(cs[i].y() - cs[j].y())
                                           : std::abs(cs[i].x() - cs[j].x());
            const double d = horizontal ? std::abs(cs[i].x() - cs[j].x())
                                        : std::abs(cs[i].y() - cs[j].y());
            if (orth < approximate_side * .22 &&
                d >= approximate_side * .90 && d <= approximate_side * 1.55)
                pitches.push_back(d);
        }
    }
    double pitch = pitches.size() >= 3 ? median(pitches) : approximate_side * 25.0 / 23.0;
    if (pitch < 4 || pitch * (length - 1) > image_length + pitch) return out;
    std::vector<double> coords;
    coords.reserve(cs.size());
    for (const auto& c : cs) coords.push_back(horizontal ? c.x() : c.y());
    if (pitches.size() < 3) {
        // JPEG chroma subsampling can connect adjacent tile silhouettes. The
        // remaining isolated components still reveal integer multiples of the
        // pitch, even when no two adjacent tiles remain separate components.
        double best_score = -1;
        const size_t limit = std::min<size_t>(coords.size(), 256);
        for (size_t a = 0; a < limit; ++a) {
            for (size_t b = a + 1; b < limit; ++b) {
                const double d = std::abs(coords[a] - coords[b]);
                const int first = std::max(1, static_cast<int>(std::ceil(d / (approximate_side * 1.45))));
                const int last = std::min(length - 1, static_cast<int>(std::floor(d / (approximate_side * .80))));
                for (int gap = first; gap <= last; ++gap) {
                    const double candidate = d / gap;
                    if (candidate < 4 || candidate * (length - 1) > image_length + candidate) continue;
                    double score = 0;
                    for (double v : coords) {
                        const double delta = std::abs((v - coords[a]) / candidate - std::round((v - coords[a]) / candidate));
                        if (delta < .16) score += 1.0 - delta;
                    }
                    if (score > best_score) { best_score = score; pitch = candidate; }
                }
            }
        }
    }
    double phase = coords[0];
    int best = -1;
    for (double candidate : coords) {
        int n = 0;
        for (double v : coords)
            if (std::abs((v - candidate) / pitch - std::round((v - candidate) / pitch)) < .16) ++n;
        if (n > best) { best = n; phase = candidate; }
    }
    for (int iteration = 0; iteration < 3; ++iteration) {
        double sum_i = 0, sum_v = 0, sum_ii = 0, sum_iv = 0;
        int count = 0;
        for (double v : coords) {
            const double idx = std::round((v - phase) / pitch);
            if (std::abs(v - phase - idx * pitch) > pitch * .18) continue;
            ++count; sum_i += idx; sum_v += v; sum_ii += idx * idx; sum_iv += idx * v;
        }
        const double den = count * sum_ii - sum_i * sum_i;
        if (count < 4 || den <= 0) return out;
        const double candidate_pitch = (count * sum_iv - sum_i * sum_v) / den;
        if (candidate_pitch < pitch * .85 || candidate_pitch > pitch * 1.15) return out;
        pitch = candidate_pitch;
        phase = (sum_v - pitch * sum_i) / count;
    }
    std::vector<int> indices;
    for (double v : coords) {
        const int index = static_cast<int>(std::lround((v - phase) / pitch));
        if (std::abs(v - phase - index * pitch) < pitch * .18) indices.push_back(index);
    }
    if (indices.empty()) return out;
    std::sort(indices.begin(), indices.end());
    int best_start = indices.front(), best_count = -1;
    double best_margin = -std::numeric_limits<double>::infinity();
    for (int start = indices.front() - length + 1; start <= indices.back(); ++start) {
        const double left = phase + (start - .5) * pitch;
        const double right = left + length * pitch;
        if (left < -pitch * .12 || right > image_length + pitch * .12) continue;
        const int count = static_cast<int>(std::upper_bound(indices.begin(), indices.end(), start + length - 1) -
                                           std::lower_bound(indices.begin(), indices.end(), start));
        // When a whole edge row/column is empty, its position is intrinsically
        // ambiguous from tiles alone. Prefer the most centered valid frame.
        const double margin = -std::abs(left - (image_length - right));
        if (count > best_count || (count == best_count && margin > best_margin)) {
            best_count = count; best_start = start; best_margin = margin;
        }
    }
    if (best_count < 4) return out;
    int distinct = 0, previous = std::numeric_limits<int>::min();
    for (int i : indices) {
        if (i >= best_start && i < best_start + length && i != previous) ++distinct;
        previous = i;
    }
    out.ok = true; out.pitch = pitch; out.center = phase + best_start * pitch;
    out.distinct = distinct; out.inliers = best_count;
    return out;
}

struct PixelClass { int color; double confidence; };

PixelClass classify(const uint8_t* pixel) {
    const double r = pixel[0], g = pixel[1], b = pixel[2];
    const double lo = std::min({r, g, b}), hi = std::max({r, g, b});
    const double mean = (r + g + b) / 3;
    if (hi - lo < 22) {
        if (mean >= 227) return {0, std::min(1.0, .80 + (mean - 227) / 45)};
        if (mean >= 151 && mean <= 216) {
            double conf = mean <= 204 ? .98 : .98 - (mean - 204) * .035;
            return {1, conf};
        }
        return {mean < 222 ? 1 : 0, .25};
    }
    int best_color = 0;
    double best_error = std::numeric_limits<double>::infinity();
    double second_error = best_error;
    for (int color = 2; color <= kColors; ++color) {
        const uint32_t base = kPalette[color];
        const double values[3] = {r, g, b};
        const double p[3] = {double((base >> 16) & 255), double((base >> 8) & 255), double(base & 255)};
        double numerator = 0, denominator = 0;
        for (int k = 0; k < 3; ++k) {
            numerator += (values[k] - p[k]) * (255 - p[k]);
            denominator += (255 - p[k]) * (255 - p[k]);
        }
        const double whitening = std::clamp(numerator / denominator, 0.0, .60);
        double error = 0;
        for (int k = 0; k < 3; ++k) {
            const double residual = values[k] - (p[k] + whitening * (255 - p[k]));
            error += residual * residual;
        }
        error = std::sqrt(error / 3);
        if (error < best_error) {
            second_error = best_error; best_error = error; best_color = color;
        } else second_error = std::min(second_error, error);
    }
    const double fit = std::clamp(1.0 - best_error / 45, .02, 1.0);
    const double separation = std::clamp((second_error - best_error) / 12, .05, 1.0);
    return {best_color, fit * (.65 + .35 * separation)};
}
} // namespace

Recognition sample_grid(const Image& image, const GridGeometry& grid) {
    Recognition result;
    result.grid = grid;
    if (!valid_image(image)) { result.message = "Invalid or excessively large RGB image."; return result; }
    if (grid.columns < 1 || grid.rows < 1 || grid.columns > 64 || grid.rows > 64 ||
        !std::isfinite(grid.left) || !std::isfinite(grid.top) ||
        !std::isfinite(grid.cell_width) || !std::isfinite(grid.cell_height) ||
        grid.cell_width < 3 || grid.cell_height < 3) {
        result.message = "Invalid grid geometry."; return result;
    }
    result.board.width = grid.columns; result.board.height = grid.rows;
    result.board.cells.assign(static_cast<size_t>(grid.columns) * grid.rows, 0);
    result.confidence.assign(result.board.cells.size(), 0);
    for (int row = 0; row < grid.rows; ++row) {
        for (int col = 0; col < grid.columns; ++col) {
            const double cx = grid.left + (col + .5) * grid.cell_width;
            const double cy = grid.top + (row + .5) * grid.cell_height;
            if (cx < 0 || cy < 0 || cx >= image.width || cy >= image.height) {
                result.message = "Grid centers extend outside the image."; return result;
            }
            std::array<double, 11> votes{};
            std::array<int, 11> counts{};
            // Center patches avoid rounded tile corners, borders and shadows.
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = std::clamp(static_cast<int>(std::lround(cx + dx * grid.cell_width * .12)), 0, image.width - 1);
                    const int y = std::clamp(static_cast<int>(std::lround(cy + dy * grid.cell_height * .12)), 0, image.height - 1);
                    const PixelClass p = classify(&image.rgb[(static_cast<size_t>(y) * image.width + x) * 3]);
                    votes[p.color] += p.confidence; ++counts[p.color];
                }
            }
            const int color = static_cast<int>(std::max_element(votes.begin(), votes.end()) - votes.begin());
            const size_t cell = static_cast<size_t>(row) * grid.columns + col;
            result.board.cells[cell] = static_cast<uint8_t>(color);
            result.confidence[cell] = static_cast<float>(votes[color] / 9);
            if (result.confidence[cell] < .70f || counts[color] < 7) ++result.uncertain_cells;
        }
    }
    result.ok = true;
    std::ostringstream message;
    message << "Recognized " << std::count_if(result.board.cells.begin(), result.board.cells.end(), [](uint8_t c) { return c != 0; })
            << " tiles; " << result.uncertain_cells << " uncertain cells.";
    result.message = message.str();
    return result;
}

Recognition recognize(const Image& image, int columns, int rows) {
    Recognition failure;
    if (!valid_image(image)) { failure.message = "Invalid or excessively large RGB image."; return failure; }
    if (columns < 1 || rows < 1 || columns > 64 || rows > 64) {
        failure.message = "Grid dimensions must be between 1 and 64."; return failure;
    }
    auto cs = components(image);
    if (cs.size() < 4) {
        failure.message = "Not enough colored tiles to locate a grid. Select the board rectangle manually.";
        return failure;
    }
    // Locate the dominant size cluster rather than assuming 25-pixel cells.
    std::vector<double> sides;
    sides.reserve(cs.size());
    for (const auto& c : cs) sides.push_back(c.side());
    std::sort(sides.begin(), sides.end());
    size_t begin = 0, best_begin = 0, best_end = 0;
    for (size_t end = 0; end < sides.size(); ++end) {
        while (sides[end] > sides[begin] * 1.23 && begin < end) ++begin;
        if (end - begin > best_end - best_begin) { best_begin = begin; best_end = end; }
    }
    const double tile_side = median(std::vector<double>(sides.begin() + best_begin, sides.begin() + best_end + 1));
    cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const Component& c) {
        return c.side() < tile_side * .84 || c.side() > tile_side * 1.16;
    }), cs.end());
    // Bound quadratic lattice fitting on screenshots containing huge icon sets.
    if (cs.size() > 2048) {
        failure.message = "Too many candidate shapes. Crop the screenshot to the game and retry.";
        return failure;
    }
    std::vector<double> widths, heights;
    for (const auto& c : cs) { widths.push_back(c.w()); heights.push_back(c.h()); }
    const AxisFit x = fit_axis(cs, true, columns, median(widths), image.width);
    const AxisFit y = fit_axis(cs, false, rows, median(heights), image.height);
    if (!x.ok || !y.ok || x.distinct < std::min(columns, 3) || y.distinct < std::min(rows, 3) ||
        std::min(x.inliers, y.inliers) < static_cast<int>(cs.size() * .60)) {
        failure.message = "A regular tile grid was not found. Select the board rectangle manually.";
        return failure;
    }
    GridGeometry grid;
    grid.left = x.center - x.pitch * .5; grid.top = y.center - y.pitch * .5;
    grid.cell_width = x.pitch; grid.cell_height = y.pitch;
    grid.columns = columns; grid.rows = rows;
    auto result = sample_grid(image, grid);
    if (result.ok && (x.distinct < columns || y.distinct < rows)) {
        result.message += " Empty edge rows or columns may make the grid origin ambiguous; verify the overlay.";
    }
    return result;
}
} // namespace ct
