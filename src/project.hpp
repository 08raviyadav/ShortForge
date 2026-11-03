#pragma once
// Project file: JSON describing a vertical short to render.
// Pure logic (no FFmpeg) so `shortforge plan` stays lightweight.
#include <string>
#include <vector>

namespace sf {

struct Scene {
    std::string image;
    double duration = 3.0;  // seconds
    double zoom_from = 1.0;
    double zoom_to = 1.12;
    // Pan position 0..1 inside the travel range, eased from start to end.
    double pan_x0 = 0.5, pan_y0 = 0.5;
    double pan_x1 = 0.5, pan_y1 = 0.5;
    std::string title;
    std::string subtitle;
};

struct Project {
    int width = 1080;
    int height = 1920;
    int fps = 30;
    std::string audio;   // optional path
    std::string output = "short.mp4";
    std::vector<Scene> scenes;

    double total_seconds() const;
};

// Throws JsonError / std::runtime_error with a human-readable message.
Project load_project(const std::string& path);

}  // namespace sf
