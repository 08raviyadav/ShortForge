#include "hwaccel.hpp"

#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace sf {

std::vector<EncoderInfo> available_video_encoders() {
    std::vector<EncoderInfo> out;
    const struct {
        const char* name;
        bool hw;
        const char* label;
    } candidates[] = {
        {"h264_nvenc", true, "NVIDIA NVENC (GPU)"},
        {"h264_qsv", true, "Intel Quick Sync Video"},
        {"h264_vaapi", true, "VAAPI"},
        {"libx264", false, "x264 (CPU)"},
        {"libopenh264", false, "OpenH264 (CPU)"},
    };
    for (const auto& c : candidates) {
        if (avcodec_find_encoder_by_name(c.name))
            out.push_back({c.name, c.hw, c.label});
    }
    if (avcodec_find_encoder(AV_CODEC_ID_MPEG4))
        out.push_back({"mpeg4", false, "MPEG-4 Part 2 (CPU fallback)"});
    return out;
}

EncoderInfo pick_video_encoder() {
    // QSV/VAAPI need explicit device setup, so the automatic pick sticks to
    // encoders that work with zero configuration. They are still listed by
    // `shortforge probe` and selectable via --encoder.
    const char* order[] = {"h264_nvenc", "libx264", "libopenh264", "mpeg4"};
    const auto all = available_video_encoders();
    for (const char* want : order)
        for (const auto& e : all)
            if (e.name == want) return e;
    if (!all.empty()) return all.front();
    throw std::runtime_error("no usable video encoder in this FFmpeg build");
}

}  // namespace sf
