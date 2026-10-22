#ifndef COMPILER_LEXER_HPP
#define COMPILER_LEXER_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <span>
#include <optional>
#include <vector>
#include <basichumanrights.hpp>

enum class Tok {
    Ident, Number, String,
    Directive,
    MacroArg,
    MacroLabel,
    Comma, Colon, LBracket, RBracket, LParen, RParen,
    Plus, Minus, Star, Slash, Percent,
    And, Or, Xor, Shl, Shr,
    Newline, End, Error
};

struct Token {
    Tok kind;
    std::string_view text;
    size_t line;
    size_t col;
    bool spaced;
};


std::vector<Token> tokenizeAll(std::string_view src);
Errorable<std::vector<std::vector<Token>>> lexLuthorLines(std::string_view src);

static inline std::optional<std::span<const Token>> getTokenSpanInsideSquareBracket(std::span<const Token> t) {
    size_t open = 0;
    while (open < t.size() && t[open].kind != Tok::LBracket) ++open;

    int depth = 0;
    for (size_t i = open; i < t.size(); ++i) {
        depth += t[i].kind == Tok::LBracket;
        depth -= t[i].kind == Tok::RBracket;
        if (!depth) return t.subspan(open + 1, i - open - 1);
    }
    return std::nullopt;
}

static inline bool getIsFullSquareBPURE(std::span<const Token> t) {
    size_t open = 0;
    if (t[open].kind != Tok::LBracket) return false;

    int depth = 0;
    bool sufferEndingSoon = false;
    for (size_t i = open+1; i < t.size(); ++i) {
        if (t[i].kind == Tok::LBracket) return false;
        if (t[i].kind == Tok::RBracket) sufferEndingSoon = true;
        else if (sufferEndingSoon && t[i].kind != Tok::End) return false;
    }
    return true;
}

#endif // COMPILER_LEXER_HPP
