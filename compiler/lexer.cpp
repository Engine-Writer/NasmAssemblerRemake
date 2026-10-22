#include <compiler/lexer.hpp>
#include <utilities/stringutils.hpp>

#include <cctype>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

static constexpr std::string_view identStart = "_.?$";
static constexpr std::string_view identBody  = "_.?$#@~";

std::vector<Token> tokenizeAll(std::string_view src) {
    std::vector<Token> out;
    size_t pos = 0;
    size_t line = 1;

    while (true) {
        bool spaced = false;
        while (
            pos < src.size() &&
            (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r')
        ) {
            ++pos;
            spaced = true;
        }
        if (pos < src.size() && src[pos] == ';')
            while (pos < src.size() && src[pos] != '\n') ++pos;

        if (pos >= src.size()) {
            out.push_back({Tok::End, src.substr(pos), line, src.size() - 1, spaced});
            return out;
        }

        size_t start = pos;

        char ch = src[pos++];
        char nx  = pos < src.size() ? src[pos] : '\0';
        char nx2 = pos + 1 < src.size() ? src[pos + 1] : '\0';

        Tok kind = Tok::Error;
        bool ident = false;

        if (
            std::isalpha(static_cast<uchar>(ch)) ||
            identStart.find(ch) != std::string_view::npos
        ) {
            kind = Tok::Ident;
            ident = true;

        } else if (std::isdigit(static_cast<uchar>(ch))) {
            while (
                pos < src.size() &&
                (std::isalnum(static_cast<uchar>(src[pos])) || src[pos] == '_')
            ) ++pos;
            kind = Tok::Number;

        } else if (ch == '"' || ch == '\'' || ch == '`') {
            while (pos < src.size() && src[pos] != ch && src[pos] != '\n') {
                if (src[pos] == '\\' && pos + 1 < src.size() && src[pos + 1] != '\n') ++pos;
                ++pos;
            }
            if (pos < src.size() && src[pos] == ch) {
                ++pos;
                kind = Tok::String;
            }
        } else if (ch == '%') {
            if (nx == '%' && (std::isalpha(static_cast<uchar>(nx2)) || nx2 == '_')) {
                ++pos;
                kind = Tok::MacroLabel;
                ident = true;
            } else if (std::isdigit(static_cast<uchar>(nx))) {
                while (pos < src.size() && std::isdigit(static_cast<uchar>(src[pos]))) ++pos;
                kind = Tok::MacroArg;
            } else if (std::isalpha(static_cast<uchar>(nx)) || nx == '_') {
                kind = Tok::Directive;
                ident = true;
            } else kind = Tok::Percent;

        } else switch (ch) {
            case ',':  kind = Tok::Comma;    break;
            case ':':  kind = Tok::Colon;    break;
            case '[':  kind = Tok::LBracket; break;
            case ']':  kind = Tok::RBracket; break;
            case '(':  kind = Tok::LParen;   break;
            case ')':  kind = Tok::RParen;   break;
            case '+':  kind = Tok::Plus;     break;
            case '-':  kind = Tok::Minus;    break;
            case '*':  kind = Tok::Star;     break;
            case '/':  kind = Tok::Slash;    break;
            case '&':  kind = Tok::And;      break;
            case '|':  kind = Tok::Or;       break;
            case '^':  kind = Tok::Xor;      break;
            case '>':  kind = Tok::Shr;      break;
            case '<':  kind = Tok::Shl;      break;
            case '\n': kind = Tok::Newline;  break;
        }

        // idents, %directives and %%labels all finish the same way
        if (ident) {
            while (
                pos < src.size() &&
                (std::isalnum(static_cast<uchar>(src[pos])) ||
                identBody.find(src[pos]) != std::string_view::npos)
            ) ++pos;
        }

        out.push_back({kind, src.substr(start, pos - start), line, start, spaced});
        if (ch == '\n') ++line;
    }
}

Errorable<std::vector<std::vector<Token>>> lexLuthorLines(std::string_view src) {
    std::vector<std::vector<Token>> lines;
    std::vector<Token> cur;

    for (const Token &t : tokenizeAll(src)) {
        if (t.kind == Tok::Error) return {
            .main{}, .error = 1,
            .errTxt = std::format("line {}: unexpected `{}`", t.line, t.text) };

        if (t.kind != Tok::Newline && t.kind != Tok::End) {
            cur.push_back(t);
            continue;
        }

        if (!cur.empty()) {
            cur.push_back({Tok::End, t.text.substr(0, 0), t.line, false, t.spaced});
            lines.push_back(std::move(cur));
            cur.clear();
        }

        if (t.kind == Tok::End) break;
    }
    return { .main = lines };
}
