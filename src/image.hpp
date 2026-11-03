#pragma once
// Plain RGB24 image buffer shared by decode / scale / overlay stages.
#include <cstdint>
#include <vector>

namespace sf {

struct Image {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px;  // RGB24, row-major

    bool empty() const { return px.empty() || w <= 0 || h <= 0; }
    int stride() const { return w * 3; }
};

}  // namespace sf
