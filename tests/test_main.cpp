// ShortForge unit tests — JSON parser, Ken Burns math, bitmap font, project file.
// No FFmpeg needed; run with: ./shortforge_tests
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bitmap_font.hpp"
#include "json.hpp"
#include "kenburns.hpp"
#include "project.hpp"

static int g_fail = 0;

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            ++g_fail;                                                           \
        }                                                                       \
    } while (0)
#define CHECK_CLOSE(a, b, eps) CHECK(std::fabs((a) - (b)) <= (eps))

static void test_json() {
    auto v = sf::parse_json(R"({"a": 1, "b": [true, null, "x"], "c": {"d": 2.5}})");
    CHECK(v.at("a").as_num() == 1.0);
    CHECK(v.at("b").as_arr()[0].as_bool() == true);
    CHECK(v.at("b").as_arr()[1].type == sf::JsonValue::Type::Null);
    CHECK(v.at("b").as_arr()[2].as_str() == "x");
    CHECK(v.at("c").at("d").as_num() == 2.5);
    CHECK(v.get_str("missing", "dflt") == "dflt");
    CHECK(v.get_num("missing", 3.0) == 3.0);
    CHECK(v.get_bool("missing", true) == true);

    auto e = sf::parse_json(R"({"s": "a\nb\u00e9"})");
    CHECK(e.at("s").as_str() == std::string("a\nb\xc3\xa9"));  // é as UTF-8

    // UTF-8 BOM (Windows editors) is skipped.
    auto bom = sf::parse_json(std::string("\xEF\xBB\xBF") + R"({"a": 1})");
    CHECK(bom.at("a").as_num() == 1.0);

    // Strict number grammar: "01" and "1." are invalid JSON.
    for (const char* badnum : {"[01]", "[1.]", "[+1]", "[.5]", "[1e]", "[--1]"}) {
        bool threw = false;
        try {
            sf::parse_json(badnum);
        } catch (const sf::JsonError&) {
            threw = true;
        }
        CHECK(threw);
    }
    // Valid numbers still parse.
    auto nums = sf::parse_json("[0, -0.5, 1e3, 2E-2, 3.14]");
    CHECK(nums.as_arr()[0].as_num() == 0.0);
    CHECK(nums.as_arr()[2].as_num() == 1000.0);

    // UTF-16 surrogate pair -> 4-byte UTF-8 (U+1F600 = F0 9F 98 80).
    auto emoji = sf::parse_json(R"({"e": "a\uD83D\uDE00b"})");
    CHECK(emoji.at("e").as_str() == std::string("a\xF0\x9F\x98\x80"
                                               "b"));
    // Lone surrogates and raw control characters are rejected.
    for (const char* badstr : {"[\"\\uDE00\"]", "[\"\\uD83D\"]", "[\"a\x01" "b\"]"}) {
        bool threw = false;
        try {
            sf::parse_json(badstr);
        } catch (const sf::JsonError&) {
            threw = true;
        }
        CHECK(threw);
    }

    for (const char* bad : {"{bad}", "[1, 2", "{\"a\":}", "", "tru"}) {
        bool threw = false;
        try {
            sf::parse_json(bad);
        } catch (const sf::JsonError&) {
            threw = true;
        }
        CHECK(threw);
    }
}

static void test_kenburns() {
    // t=0, zoom 1.0 -> whole base image visible.
    auto r0 = sf::kenburns_crop(0.0, 1440, 2560, 1080, 1920, 1.0, 1.2, 0.5, 0.5, 0.5, 0.5);
    CHECK_CLOSE(r0.w, 1440, 1e-6);
    CHECK_CLOSE(r0.h, 2560, 1e-6);
    CHECK_CLOSE(r0.x, 0, 1e-6);
    CHECK_CLOSE(r0.y, 0, 1e-6);

    // t=1, zoomed in, panned to bottom-right corner; aspect preserved.
    auto r1 = sf::kenburns_crop(1.0, 1440, 2560, 1080, 1920, 1.0, 1.2, 0.0, 0.0, 1.0, 1.0);
    CHECK_CLOSE(r1.w / r1.h, 1080.0 / 1920.0, 1e-9);
    CHECK(r1.w < r0.w);
    CHECK_CLOSE(r1.x, 1440 - r1.w, 1e-6);
    CHECK_CLOSE(r1.y, 2560 - r1.h, 1e-6);

    // Mid-flight: inside bounds, aspect preserved.
    auto rm = sf::kenburns_crop(0.5, 1440, 2560, 1080, 1920, 1.0, 1.5, 0.2, 0.8, 0.9, 0.1);
    CHECK(rm.x >= -1e-9 && rm.x + rm.w <= 1440 + 1e-6);
    CHECK(rm.y >= -1e-9 && rm.y + rm.h <= 2560 + 1e-6);
    CHECK_CLOSE(rm.w / rm.h, 1080.0 / 1920.0, 1e-9);

    // Easing sanity.
    CHECK_CLOSE(sf::kb_ease(0.0), 0.0, 1e-9);
    CHECK_CLOSE(sf::kb_ease(1.0), 1.0, 1e-9);
    const double mid = sf::kb_ease(0.5);
    CHECK(mid > 0.49 && mid < 0.51);
}

static void test_kenburns_continuity() {
    // Regression: on wide/square sources the crop must shrink smoothly with
    // zoom — no mid-scene jump cuts (old code compared the zoomed height
    // against the unzoomed base height).
    double prev_w = 1e9, prev_h = 1e9;
    for (int k = 0; k <= 40; ++k) {
        const double z = 1.0 + 1.0 * k / 40;  // 1.0 -> 2.0
        auto r = sf::kenburns_crop(0.5, 1920, 1920, 1080, 1920, z, z, 0.5, 0.5, 0.5, 0.5);
        CHECK_CLOSE(r.w / r.h, 1080.0 / 1920.0, 1e-9);
        CHECK(r.w <= prev_w + 1e-9 && r.h <= prev_h + 1e-9);  // monotonic shrink
        if (k > 0) CHECK(prev_w - r.w < 60.0);                // no jumps
        CHECK(r.x >= -1e-9 && r.x + r.w <= 1920 + 1e-6);
        CHECK(r.y >= -1e-9 && r.y + r.h <= 1920 + 1e-6);
        prev_w = r.w;
        prev_h = r.h;
    }
}

static void test_font() {
    CHECK(sf::font_glyph('A')[0] == 0x0E);
    CHECK(sf::font_glyph('a')[0] == 0x0E);  // lowercase maps to uppercase
    CHECK(sf::font_glyph('~')[0] == 0x00);  // unknown -> blank
    CHECK(sf::text_width("AB", 2) == 2 * 6 * 2 - 2);
    CHECK(sf::text_width("", 2) == 0);  // empty string has zero width

    const int W = 100, H = 50;
    std::vector<uint8_t> buf(static_cast<size_t>(W) * H * 3, 0);
    sf::draw_text(buf.data(), W, H, "A", W / 2, 5, 2, 255, 255, 255);
    bool any = false;
    for (uint8_t b : buf)
        if (b) {
            any = true;
            break;
        }
    CHECK(any);

    std::vector<uint8_t> buf2(static_cast<size_t>(W) * H * 3, 100);
    sf::draw_bar(buf2.data(), W, H, 0, 0, W, H, 0, 0, 0, 255);
    CHECK(buf2[0] == 0 && buf2[1] == 0 && buf2[2] == 0);
}

static void test_project() {
    // Portable temp dir: /tmp does not exist on Windows.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "shortforge_tests";
    fs::create_directories(dir);
    const fs::path path = dir / "sf_test_project.json";
    {
        std::ofstream f(path);
        f << R"({
          "width": 1080, "height": 1920, "fps": 30,
          "audio": "music.wav",
          "output": "out.mp4",
          "scenes": [
            {"image": "a.png", "duration": 2.5, "zoom": [1.0, 1.2],
             "pan": [0.5, 0.5, 0.2, 0.8], "title": "Hello", "subtitle": "World"},
            {"image": "b.png"}
          ]
        })";
    }
    sf::Project p = sf::load_project(path.string());
    CHECK(p.width == 1080 && p.height == 1920 && p.fps == 30);
    CHECK(p.scenes.size() == 2);
    CHECK(p.scenes[0].duration == 2.5);
    CHECK(p.scenes[0].zoom_to == 1.2);
    CHECK(p.scenes[0].pan_x1 == 0.2);
    CHECK(p.scenes[0].title == "Hello");
    CHECK(p.scenes[0].subtitle == "World");
    CHECK(p.scenes[1].duration == 3.0);  // default
    CHECK_CLOSE(p.total_seconds(), 5.5, 1e-9);
    // Paths resolve relative to the project file's directory.
    CHECK(fs::path(p.scenes[0].image) == dir / "a.png");
    CHECK(fs::path(p.audio) == dir / "music.wav");
    CHECK(fs::path(p.output) == dir / "out.mp4");

    bool threw = false;
    try {
        sf::load_project((dir / "sf_does_not_exist.json").string());
    } catch (...) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        const fs::path bad = dir / "sf_bad_project.json";
        std::ofstream f(bad);
        f << R"({"scenes": [{"duration": 1}]})";
        f.close();
        sf::load_project(bad.string());
    } catch (const sf::JsonError&) {
        threw = true;
    }
    CHECK(threw);

    // Clean up.
    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(dir / "sf_bad_project.json", ec);
    fs::remove(dir, ec);
}

int main() {
    test_json();
    test_kenburns();
    test_kenburns_continuity();
    test_font();
    test_project();
    if (g_fail == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_fail);
    return 1;
}
