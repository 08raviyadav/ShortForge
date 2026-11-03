#pragma once
// FFmpeg-backed media decoding: images -> RGB24, audio -> 44.1kHz stereo s16.
#include <cstdint>
#include <string>
#include <vector>

#include "image.hpp"

namespace sf {

// Decode the first frame of an image file (png/jpg/bmp/webp/...) to RGB24.
// Throws std::runtime_error on failure.
Image decode_image(const std::string& path);

struct AudioClip {
    int sample_rate = 44100;
    int channels = 2;
    std::vector<int16_t> pcm;  // interleaved s16, normalized to 44.1kHz stereo
    bool empty() const { return pcm.empty(); }
};

// Decode an audio file (wav/mp3/m4a/...) to 44.1kHz stereo s16.
// Throws std::runtime_error on failure.
AudioClip decode_audio(const std::string& path);

}  // namespace sf
