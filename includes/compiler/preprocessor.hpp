#ifndef COMPILER_PREPROCESSOR_HPP
#define COMPILER_PREPROCESSOR_HPP

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <compiler/lexer.hpp>
#include <basichumanrights.hpp>

struct Tk {
    Tok kind;
    std::string text;
    size_t line;
    bool spaced;
};

using TkLine = std::vector<Tk>;
using Args = std::vector<TkLine>;

struct Macro {
    size_t nargs = 0;
    std::vector<TkLine> body;
};

struct Define {
    bool fn = false;
    std::vector<std::string> params;
    TkLine body;
};

struct Expanded {
    std::string text;
    std::vector<int> lineMap;
};


struct Preprocessor {
    std::unordered_map<std::string, Macro> m_macros;
    std::unordered_map<std::string, Define> m_defines;
    int m_uid = 0;
    static constexpr int kMaxDepth = 32;
    Expanded m_out;


    Errorable<std::vector<TkLine>> collect(std::vector<TkLine> lines);
    Errorable<TkLine> expandDefines(const TkLine &in, std::vector<std::string> hide);
    Errorable<bool> directive(const TkLine &ln);
    Errorable<TkLine> substitute(const TkLine &body, const Args &args, int id, size_t line);
    Errorable<bool> process(TkLine ln, int srcLine, int depth);

    Errorable<Expanded> run(std::string_view src);
    const std::unordered_map<std::string, Define> &getDefines() const { return m_defines; }
};


#endif // COMPILER_PREPROCESSOR_HPP
