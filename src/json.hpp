#pragma once
// Minimal zero-dependency JSON parser (subset sufficient for project files).
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sf {

class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string& msg) : std::runtime_error(msg) {}
};

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;  // ordered

    bool as_bool() const;
    double as_num() const;
    const std::string& as_str() const;
    const std::vector<JsonValue>& as_arr() const;

    // Object lookup. Returns nullptr when the key is absent or not an object.
    const JsonValue* find(const std::string& key) const;
    // Required lookup; throws JsonError when absent.
    const JsonValue& at(const std::string& key) const;

    // Helpers with defaults (missing key or wrong type -> default).
    std::string get_str(const std::string& key, const std::string& dflt = "") const;
    double get_num(const std::string& key, double dflt = 0.0) const;
    bool get_bool(const std::string& key, bool dflt = false) const;
};

JsonValue parse_json(const std::string& text);
std::string read_file(const std::string& path);

}  // namespace sf
