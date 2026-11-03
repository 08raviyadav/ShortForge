#include "json.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sf {

bool JsonValue::as_bool() const {
    if (type != Type::Bool) throw JsonError("expected bool");
    return boolean;
}
double JsonValue::as_num() const {
    if (type != Type::Number) throw JsonError("expected number");
    return number;
}
const std::string& JsonValue::as_str() const {
    if (type != Type::String) throw JsonError("expected string");
    return str;
}
const std::vector<JsonValue>& JsonValue::as_arr() const {
    if (type != Type::Array) throw JsonError("expected array");
    return arr;
}
const JsonValue* JsonValue::find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}
const JsonValue& JsonValue::at(const std::string& key) const {
    const JsonValue* v = find(key);
    if (!v) throw JsonError("missing key: " + key);
    return *v;
}
std::string JsonValue::get_str(const std::string& key, const std::string& dflt) const {
    const JsonValue* v = find(key);
    return (v && v->type == Type::String) ? v->str : dflt;
}
double JsonValue::get_num(const std::string& key, double dflt) const {
    const JsonValue* v = find(key);
    return (v && v->type == Type::Number) ? v->number : dflt;
}
bool JsonValue::get_bool(const std::string& key, bool dflt) const {
    const JsonValue* v = find(key);
    return (v && v->type == Type::Bool) ? v->boolean : dflt;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw JsonError("cannot open file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

namespace {

void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
}

struct Parser {
    const std::string& s;
    size_t i = 0;
    explicit Parser(const std::string& t) : s(t) {}

    [[noreturn]] void fail(const std::string& m) {
        throw JsonError("parse error at offset " + std::to_string(i) + ": " + m);
    }
    char peek() {
        if (i >= s.size()) fail("unexpected end of input");
        return s[i];
    }
    char get() {
        if (i >= s.size()) fail("unexpected end of input");
        return s[i++];
    }
    void expect(char c) {
        char got = get();
        if (got != c) fail(std::string("expected '") + c + "' but got '" + got + "'");
    }
    void expect_lit(const char* lit) {
        for (const char* p = lit; *p; ++p)
            if (get() != *p) fail("bad literal");
    }

    void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char e = get();
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = get();
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                            else fail("bad \\u escape");
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default: fail("bad escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    double parse_number() {
        size_t start = i;
        if (peek() == '-') get();
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
        if (i < s.size() && s[i] == '.') {
            i++;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            i++;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
        }
        std::string tok = s.substr(start, i - start);
        char* end = nullptr;
        double v = std::strtod(tok.c_str(), &end);
        if (end != tok.c_str() + tok.size()) fail("bad number");
        return v;
    }

    JsonValue parse_value() {
        skip_ws(s, i);
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') {
            JsonValue v;
            v.type = JsonValue::Type::String;
            v.str = parse_string();
            return v;
        }
        if (c == 't') {
            expect_lit("true");
            JsonValue v;
            v.type = JsonValue::Type::Bool;
            v.boolean = true;
            return v;
        }
        if (c == 'f') {
            expect_lit("false");
            JsonValue v;
            v.type = JsonValue::Type::Bool;
            v.boolean = false;
            return v;
        }
        if (c == 'n') {
            expect_lit("null");
            return JsonValue{};
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            JsonValue v;
            v.type = JsonValue::Type::Number;
            v.number = parse_number();
            return v;
        }
        fail("unexpected character");
    }

    JsonValue parse_array() {
        JsonValue v;
        v.type = JsonValue::Type::Array;
        expect('[');
        skip_ws(s, i);
        if (peek() == ']') {
            get();
            return v;
        }
        while (true) {
            v.arr.push_back(parse_value());
            skip_ws(s, i);
            char c = get();
            if (c == ']') break;
            if (c != ',') fail("expected ',' or ']' in array");
        }
        return v;
    }

    JsonValue parse_object() {
        JsonValue v;
        v.type = JsonValue::Type::Object;
        expect('{');
        skip_ws(s, i);
        if (peek() == '}') {
            get();
            return v;
        }
        while (true) {
            skip_ws(s, i);
            if (peek() != '"') fail("expected string key");
            std::string key = parse_string();
            skip_ws(s, i);
            expect(':');
            JsonValue val = parse_value();
            v.obj.emplace_back(std::move(key), std::move(val));
            skip_ws(s, i);
            char c = get();
            if (c == '}') break;
            if (c != ',') fail("expected ',' or '}' in object");
        }
        return v;
    }
};

}  // namespace

JsonValue parse_json(const std::string& text) {
    Parser p(text);
    JsonValue v = p.parse_value();
    skip_ws(text, p.i);
    if (p.i != text.size()) throw JsonError("trailing characters after JSON value");
    return v;
}

}  // namespace sf
