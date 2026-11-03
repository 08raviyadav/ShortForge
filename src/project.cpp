#include "project.hpp"

#include <filesystem>
#include <stdexcept>

#include "json.hpp"

namespace sf {

double Project::total_seconds() const {
    double t = 0.0;
    for (const auto& s : scenes) t += s.duration;
    return t;
}

namespace {

Scene parse_scene(const JsonValue& v) {
    Scene s;
    s.image = v.get_str("image");
    if (s.image.empty()) throw JsonError("scene is missing required \"image\"");
    s.duration = v.get_num("duration", 3.0);
    if (s.duration <= 0.0) throw JsonError("scene duration must be > 0");

    const JsonValue* zoom = v.find("zoom");
    if (zoom) {
        const auto& a = zoom->as_arr();
        if (a.size() != 2) throw JsonError("\"zoom\" must be [from, to]");
        s.zoom_from = a[0].as_num();
        s.zoom_to = a[1].as_num();
        if (s.zoom_from < 1.0 || s.zoom_to < 1.0) throw JsonError("\"zoom\" values must be >= 1.0");
    }
    const JsonValue* pan = v.find("pan");
    if (pan) {
        const auto& a = pan->as_arr();
        if (a.size() != 4) throw JsonError("\"pan\" must be [x0, y0, x1, y1]");
        s.pan_x0 = a[0].as_num();
        s.pan_y0 = a[1].as_num();
        s.pan_x1 = a[2].as_num();
        s.pan_y1 = a[3].as_num();
    }
    s.title = v.get_str("title");
    s.subtitle = v.get_str("subtitle");
    return s;
}

}  // namespace

Project load_project(const std::string& path) {
    Project p;
    JsonValue root = parse_json(read_file(path));
    if (root.type != JsonValue::Type::Object) throw JsonError("project root must be an object");

    // Asset paths in the project file are relative to the project file itself.
    namespace fs = std::filesystem;
    const fs::path base = fs::path(path).parent_path();
    auto resolve = [&](const std::string& rel) -> std::string {
        if (rel.empty()) return rel;
        const fs::path pp(rel);
        if (pp.is_absolute() || base.empty()) return rel;
        return (base / pp).string();
    };

    p.width = static_cast<int>(root.get_num("width", 1080));
    p.height = static_cast<int>(root.get_num("height", 1920));
    p.fps = static_cast<int>(root.get_num("fps", 30));
    if (p.width <= 0 || p.height <= 0 || p.fps <= 0)
        throw JsonError("width/height/fps must be positive");
    if ((p.width & 1) || (p.height & 1))
        throw JsonError("width and height must be even (H.264 requirement)");

    p.audio = resolve(root.get_str("audio"));
    p.output = root.get_str("output", "short.mp4");

    const JsonValue& scenes = root.at("scenes");
    for (const auto& sv : scenes.as_arr()) {
        Scene s = parse_scene(sv);
        s.image = resolve(s.image);
        p.scenes.push_back(std::move(s));
    }
    if (p.scenes.empty()) throw JsonError("\"scenes\" must not be empty");
    return p;
}

}  // namespace sf
