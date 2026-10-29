// Optional recognition test driver. The distributed app has no test dependencies.
#include "core.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void self_test() {
    ct::Image image;
    image.width = 55; image.height = 5;
    image.rgb.resize(image.width * image.height * 3);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto color = ct::kPalette[x / 5];
            const int i = (y * image.width + x) * 3;
            image.rgb[i] = static_cast<uint8_t>(color >> 16);
            image.rgb[i + 1] = static_cast<uint8_t>(color >> 8);
            image.rgb[i + 2] = static_cast<uint8_t>(color);
        }
    }
    ct::GridGeometry grid;
    grid.columns = 11; grid.rows = 1;
    grid.cell_width = grid.cell_height = 5;
    auto result = ct::sample_grid(image, grid);
    require(result.ok && result.uncertain_cells == 0, "Reference palette was uncertain.");
    for (int c = 0; c <= 10; ++c)
        require(result.board.cells[c] == c, "Reference palette was classified incorrectly.");

    auto invalid_image = image;
    invalid_image.rgb.pop_back();
    require(!ct::recognize(invalid_image).ok, "Truncated RGB buffer was accepted.");
    require(!ct::sample_grid(invalid_image, grid).ok, "Truncated RGB buffer was sampled.");
    auto invalid_grid = grid;
    invalid_grid.cell_width = std::numeric_limits<double>::quiet_NaN();
    require(!ct::sample_grid(image, invalid_grid).ok, "Nonfinite grid was accepted.");
    invalid_grid = grid; invalid_grid.left = image.width;
    require(!ct::sample_grid(image, invalid_grid).ok, "Outside-image grid was accepted.");
    invalid_grid = grid; invalid_grid.columns = 0;
    require(!ct::sample_grid(image, invalid_grid).ok, "Zero-width grid was accepted.");

    ct::Image dark;
    dark.width = dark.height = 5; dark.rgb.assign(75, 0);
    grid.columns = grid.rows = 1;
    result = ct::sample_grid(dark, grid);
    require(result.ok && result.uncertain_cells == 1,
            "Unknown dark cell should be flagged for manual review.");
}

std::string token(std::istream& in) {
    while (true) {
        in >> std::ws;
        if (in.peek() != '#') break;
        std::string line;
        std::getline(in, line);
    }
    std::string result;
    in >> result;
    return result;
}

ct::Image read_ppm(const char* name) {
    std::ifstream in(name, std::ios::binary);
    require(bool(in), "Cannot open PPM fixture.");
    require(token(in) == "P6", "Expected a binary RGB P6 PPM image.");
    ct::Image result;
    result.width = std::stoi(token(in));
    result.height = std::stoi(token(in));
    require(std::stoi(token(in)) == 255, "PPM fixture must use 8-bit channels.");
    require(result.width > 0 && result.height > 0 && result.width <= 16384 &&
            result.height <= 16384 && uint64_t(result.width) * result.height <= 64000000,
            "Invalid PPM fixture dimensions.");
    const char separator = static_cast<char>(in.get());
    require(separator == '\n' || separator == '\r' || separator == ' ' || separator == '\t',
            "Missing PPM raster separator.");
    if (separator == '\r' && in.peek() == '\n') in.get();
    result.rgb.resize(static_cast<size_t>(result.width) * result.height * 3);
    in.read(reinterpret_cast<char*>(result.rgb.data()), static_cast<std::streamsize>(result.rgb.size()));
    require(in.gcount() == static_cast<std::streamsize>(result.rgb.size()), "Truncated PPM fixture.");
    return result;
}

void print_json(const ct::Recognition& result) {
    std::cout << "{\"ok\":" << (result.ok ? "true" : "false")
              << ",\"uncertain\":" << result.uncertain_cells
              << ",\"grid\":[" << result.grid.left << ',' << result.grid.top << ','
              << result.grid.cell_width << ',' << result.grid.cell_height << ']'
              << ",\"cells\":[";
    for (size_t i = 0; i < result.board.cells.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << int(result.board.cells[i]);
    }
    std::cout << "]}\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            self_test();
            std::cout << "{\"self_test\":\"passed\"}\n";
            return 0;
        }
        if (argc != 2) {
            std::cerr << "Usage: test_recognition --self-test | image.ppm\n";
            return 2;
        }
        print_json(ct::recognize(read_ppm(argv[1])));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
