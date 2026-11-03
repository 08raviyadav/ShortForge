// ShortForge unit tests — JSON parser, Ken Burns math, bitmap font, project file.
// No FFmpeg needed; run with: ./shortforge_tests
#include <cmath>
#include <cstdio>
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

static void test_font() {
    CHECK(sf::font_glyph('A')[0] == 0x0E);
    CHECK(sf::font_glyph('a')[0] == 0x0E);  // lowercase maps to uppercase
    CHECK(sf::font_glyph('~')[0] == 0x00);  // unknown -> blank
    CHECK(sf::text_width("AB", 2) == 2 * 6 * 2 - 2);

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
    const char* path = "/tmp/sf_test_project.json";
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
    sf::Project p = sf::load_project(path);
    CHECK(p.width == 1080 && p.height == 1920 && p.fps == 30);
    CHECK(p.scenes.size() == 2);
    CHECK(p.scenes[0].duration == 2.5);
    CHECK(p.scenes[0].zoom_to == 1.2);
    CHECK(p.scenes[0].pan_x1 == 0.2);
    CHECK(p.scenes[0].title == "Hello");
    CHECK(p.scenes[0].subtitle == "World");
    CHECK(p.scenes[1].duration == 3.0);  // default
    CHECK_CLOSE(p.total_seconds(), 5.5, 1e-9);
    // Paths resolve relative to the project file's directory (/tmp).
    CHECK(p.scenes[0].image == "/tmp/a.png");

    bool threw = false;
    try {
        sf::load_project("/tmp/sf_does_not_exist.json");
    } catch (...) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        std::ofstream f("/tmp/sf_bad_project.json");
        f << R"({"scenes": [{"duration": 1}]})";
        f.close();
        sf::load_project("/tmp/sf_bad_project.json");
    } catch (const sf::JsonError&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    test_json();
    test_kenburns();
    test_font();
    test_project();
    if (g_fail == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_fail);
    return 1;
}
