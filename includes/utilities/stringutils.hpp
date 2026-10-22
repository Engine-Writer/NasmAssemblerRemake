#ifndef UTILITIES_STRINGUTILS_HPP
#define UTILITIES_STRINGUTILS_HPP

#include <cctype>
#include <cwctype>
#include <string>
#include <string_view>

using uchar = unsigned char;

static inline std::string capitalizeStr(const std::string &instr) {
    std::string tmp = instr;
    for (auto &c : tmp) { if (c >= 'a' && c <= 'z') c += 'A'-'a'; }
    return tmp;
}

static inline bool nextLine(std::string_view &rest, std::string_view &line) {
    if (rest.empty()) return false;

    size_t pos = rest.find('\n');
    line = rest.substr(0, pos);
    rest.remove_prefix(pos == std::string_view::npos ? rest.size() : pos + 1);
    return true;
}

static inline std::string_view getLine(const std::string &file, size_t lineNum) {
    size_t start = 0;

    for (size_t cur = 1; cur < lineNum; ++cur) {
        size_t nl = file.find('\n', start);
        if (nl == std::string::npos) return {};
        start = nl + 1;
    }

    if (start >= file.size()) return {};

    size_t end = file.find('\n', start);
    if (end == std::string::npos) end = file.size();

    std::string_view line(file.data() + start, end - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    return line;
}

#endif // UTILITIES_STRINGUTILS_HPP 
