#pragma once
// Hardware-acceleration probing: pick the fastest H.264 encoder the
// FFmpeg build (and machine) actually provides.
#include <string>
#include <vector>

namespace sf {

struct EncoderInfo {
    std::string name;   // FFmpeg encoder name
    bool hardware = false;
    std::string label;  // human-readable
};

// All usable video encoders, best first.
std::vector<EncoderInfo> available_video_encoders();

// Best encoder by preference: NVENC -> x264 -> MPEG-4 fallback.
// Throws if nothing usable exists.
EncoderInfo pick_video_encoder();

}  // namespace sf
