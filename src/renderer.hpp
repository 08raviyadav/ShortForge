#pragma once
// The render pipeline: images + Ken Burns + text overlays + audio -> MP4.
#include <string>

#include "project.hpp"

namespace sf {

struct RenderStats {
    std::string encoder;
    int frames = 0;
    double seconds = 0.0;
    long long bytes = 0;
};

// Render a project to out_path. encoder_override may be empty (auto-pick).
// Throws std::runtime_error on failure.
RenderStats render_project(const Project& p, const std::string& out_path,
                           const std::string& encoder_override = "");

}  // namespace sf
