// shortforge CLI: render | plan | probe
#include <cstdio>
#include <string>
#include <vector>

#include "hwaccel.hpp"
#include "project.hpp"
#include "renderer.hpp"

namespace {

void usage() {
    std::printf(
        "shortforge — blazing-fast local vertical video renderer\n"
        "\n"
        "Usage:\n"
        "  shortforge render <project.json> [-o out.mp4] [--encoder NAME]\n"
        "  shortforge plan   <project.json>\n"
        "  shortforge probe\n"
        "\n"
        "Examples:\n"
        "  shortforge render assets/sample_project.json -o short.mp4\n"
        "  shortforge render project.json --encoder h264_nvenc\n");
}

int cmd_probe() {
    const auto encs = sf::available_video_encoders();
    std::printf("Available video encoders (best first):\n");
    for (const auto& e : encs)
        std::printf("  %-14s %s%s\n", e.name.c_str(), e.label.c_str(),
                    e.hardware ? " [HW]" : "");
    try {
        std::printf("Auto-pick: %s\n", sf::pick_video_encoder().name.c_str());
    } catch (const std::exception& e) {
        std::printf("Auto-pick failed: %s\n", e.what());
        return 1;
    }
    return 0;
}

int cmd_plan(const std::string& path) {
    const sf::Project p = sf::load_project(path);
    std::printf("Project %dx%d @ %dfps — %zu scene(s), %.1fs total\n", p.width, p.height, p.fps,
                p.scenes.size(), p.total_seconds());
    if (!p.audio.empty()) std::printf("Audio: %s\n", p.audio.c_str());
    std::printf("Output: %s\n\n", p.output.c_str());
    int i = 0;
    for (const auto& s : p.scenes) {
        std::printf("  [%d] %-28s %.1fs  zoom %.2f->%.2f", i++, s.image.c_str(), s.duration,
                    s.zoom_from, s.zoom_to);
        if (!s.title.empty()) std::printf("  \"%s\"", s.title.c_str());
        std::printf("\n");
    }
    std::printf("\nEstimated frames: %d\n",
                static_cast<int>(p.total_seconds() * p.fps + 0.5));
    return 0;
}

int cmd_render(const std::vector<std::string>& args) {
    std::string json, out, encoder;
    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "-o" || args[i] == "--output") && i + 1 < args.size()) {
            out = args[++i];
        } else if (args[i] == "--encoder" && i + 1 < args.size()) {
            encoder = args[++i];
        } else if (!args[i].empty() && args[i][0] != '-') {
            json = args[i];
        } else {
            usage();
            return 1;
        }
    }
    if (json.empty()) {
        usage();
        return 1;
    }
    const sf::Project p = sf::load_project(json);
    if (out.empty()) out = p.output;
    const sf::RenderStats st = sf::render_project(p, out, encoder);
    std::printf("Done: %s\n  %d frames, %.1fs, encoder %s, %.1f MB\n", out.c_str(), st.frames,
                st.seconds, st.encoder.c_str(), st.bytes / 1048576.0);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    const std::string cmd = argv[1];
    try {
        if (cmd == "probe") return cmd_probe();
        if (cmd == "plan") {
            if (argc < 3) {
                usage();
                return 1;
            }
            return cmd_plan(argv[2]);
        }
        if (cmd == "render") {
            std::vector<std::string> args;
            for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);
            return cmd_render(args);
        }
        usage();
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
