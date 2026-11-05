#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cydoc_native {
inline std::string encode(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += char(c);
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
// The host supplies the absolute file URI, using its platform path API.
inline std::string link(std::string base, std::string_view locale, std::string_view page,
                        std::string_view section = {},
                        const std::vector<std::pair<std::string, std::string>>& state = {}) {
    base.erase(base.find('#') == std::string::npos ? base.size() : base.find('#'));
    base += "#/" + encode(locale) + "/" + encode(page);
    if (!section.empty()) base += "/" + encode(section);
    bool first = true;
    for (const auto& [key, value] : state) { base += first ? '?' : '&'; first = false; base += encode(key) + "=" + encode(value); }
    return base;
}
}
