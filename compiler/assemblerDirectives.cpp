#include <compiler/assemblerDirectives.hpp>
#include <utilities/stringutils.hpp>

#include <charconv>
#include <cstdint>
#include <format>
#include <compiler/assembler.hpp>
#include <stdexcept>

AtomicEvalReturn evalAtomic(const AssemblerState &st, const Token &t) {
    if (t.kind == Tok::Number) {
        std::string_view s = t.text;
        int base = 10;

        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s.remove_prefix(2);
        } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
            base = 2;
            s.remove_prefix(2);
        } else if (s.size() > 1 && (s.back() == 'h' || s.back() == 'H')) {
            base = 16;
            s.remove_suffix(1);
        }

        uint64_t v = 0;
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
        if (ec != std::errc{} || p != s.data() + s.size()) return {
            .errorId = Errorcodes::FailedToSolveExpr,
            .errTxt{"could not resolve number"}
        };
        return { .exprVal = v };
    }

    if (t.kind == Tok::String && t.text.size() == 3) {
        return { .exprVal = static_cast<uchar>(t.text[1]) };
    }

    if (t.kind == Tok::Ident) {
        if (t.text == "$") return { .exprVal = st.here() };
        if (t.text == "$$") return { .exprVal = st.initialOrigin };

        std::string name(t.text);
        auto lutName = labelLUTName(st, name);
        if (lutName.error) return {
            .errorId = static_cast<Errorcodes>(lutName.error),
            .errTxt = name
        };

        auto it = st.symbols.find(lutName.main);
        if (it == st.symbols.end()) return {
            .errorId = Errorcodes::UndefinedSymbol,
            .errTxt = name,
        };
        return { .exprVal = it->second };
    }

    return {
        .errorId = Errorcodes::ExpectedValue,
        .errTxt{std::string(t.text)},
    };
}

static int prec(Tok k) {
    switch (k) {
        case Tok::Or:    return 1;
        case Tok::Xor:   return 2;
        case Tok::And:   return 3;
        case Tok::Shl:
        case Tok::Shr:   return 4;
        case Tok::Plus:
        case Tok::Minus: return 5;
        case Tok::Star:
        case Tok::Slash: return 6;
        default:         return -1;
    }
}

static bool endsValue(Tok k) {
    switch (k) {
        case Tok::Number:
        case Tok::String:
        case Tok::Ident:
        case Tok::RParen: return true;
        default:          return false;
    }
}

AtomicEvalReturn solveExpr(const AssemblerState &st, std::span<const Token> t) {
    while (t.size() > 1 && t.front().kind == Tok::LParen) {
        int d = 0;
        size_t i = 0;
        for (; i < t.size(); ++i) {
            d += t[i].kind == Tok::LParen;
            d -= t[i].kind == Tok::RParen;
            if (!d) break;
        }
        if (i != t.size() - 1) break;
        t = t.subspan(1, t.size() - 2);
    }
    if (t.empty()) return {
        .errorId = Errorcodes::ExpectedValue,
        .errTxt{"nothing"},
    };


    int depth = 0, best = INT_MAX;
    size_t at = 0, len = 1;
    bool found = false;

    for (size_t i = 0; i < t.size(); ++i) {
        Tok k = t[i].kind;
        if (k == Tok::LParen) ++depth;
        if (k == Tok::RParen && --depth < 0) return {
            .errorId = Errorcodes::CustomError,
            .errTxt{"unbalanced `)`"},
        };

        int p = prec(k);
        if (depth || p < 0 || !i || !endsValue(t[i-1].kind)) continue;

        size_t n = 1;
        if (k == Tok::Shl || k == Tok::Shr) {
            if (i + 1 >= t.size() || t[i+1].kind != k) return {
                .errorId = Errorcodes::CustomError,
                .errTxt{"expected `<<` or `>>`"},
            };
            n = 2;
        }
        if (p <= best) { best = p; at = i; len = n; found = true; }
        i += n - 1;
    }
    if (depth) return {
        .errorId = Errorcodes::CustomError,
        .errTxt{"unbalanced `(`"},
    };

    if (found) {
        auto l = solveExpr(st, t.first(at));
        if (l.errorId != Errorcodes::NoError) return l;
        auto r = solveExpr(st, t.subspan(at + len));
        if (r.errorId != Errorcodes::NoError) return r;

        uint64_t a = l.exprVal, b = r.exprVal;
        switch (t[at].kind) {
            case Tok::Plus:  return { .exprVal = a + b };
            case Tok::Minus: return { .exprVal = a - b };
            case Tok::Star:  return { .exprVal = a * b };
            case Tok::And:   return { .exprVal = a & b };
            case Tok::Or:    return { .exprVal = a | b };
            case Tok::Xor:   return { .exprVal = a ^ b };
            case Tok::Shl:   return { .exprVal = b < 64 ? a << b : 0 };
            case Tok::Shr:   return { .exprVal = b < 64 ? a >> b : 0 };
            case Tok::Slash:
                if (!b) return {
                    .errorId = Errorcodes::DivisionByZero,
                };
                return { .exprVal = a / b };
            default: break;
        }
    }

    // unary + / -
    if (t.front().kind == Tok::Minus || t.front().kind == Tok::Plus) {
        auto v = solveExpr(st, t.subspan(1));
        if (v.errorId == Errorcodes::NoError && t.front().kind == Tok::Minus) v.exprVal = -v.exprVal;
        return v;
    }

    if (t.size() == 1) return evalAtomic(st, t[0]);

    return {
        .errorId = Errorcodes::FailedToSolveExpr,
        .errTxt{"malformed expression"},
    };
}

static ParserOutput singleArg(AssemblerState &st, std::span<const Token> ops, uint64_t &out) {
    if (ops.size() < 2) return {
        .errorId = Errorcodes::InstrTakesValues,
        .errTxt{"1", "0"},
    };

    int commas = 0;
    for (auto &tok : ops) commas += tok.kind == Tok::Comma;
    if (commas) return {
        .errorId = Errorcodes::InstrTakesValues,
        .errTxt{"1", std::format("{}", commas + 1)},
    };

    auto r = solveExpr(st, ops.subspan(1));
    if (r.errorId != Errorcodes::NoError) return {
        .parsed = ops.subspan(1),
        .errorId = r.errorId,
        .errTxt{r.errTxt},
    };

    out = r.exprVal;
    return {};
}

template <size_t Size>
ParserOutput dataHandler(AssemblerState &st, std::span<const Token> ops) {
    if (ops.size() < 2) return {
        .errorId = Errorcodes::InstrTakesValues,
        .errTxt{"1", "0"},
    };

    auto args = ops.subspan(1);
    int depth = 0;
    size_t start = 0;

    for (size_t i = 0; i <= args.size(); ++i) {
        if (i < args.size()) {
            Tok k = args[i].kind;
            depth += k == Tok::LParen;
            depth -= k == Tok::RParen;
            if (k != Tok::Comma || depth) continue;
        }

        auto part = args.subspan(start, i - start);
        start = i + 1;

        if (part.empty()) return {
            .parsed = args,
            .errorId = Errorcodes::CustomError,
            .errTxt{"empty value, stray `,`"},
        };

        if (part.size() == 1 && part[0].kind == Tok::String && part[0].text.size() != 3) {
            std::vector<std::byte> instr{};

            auto vxc = part[0].text.substr(1, part[0].text.size() - 2);
            for (const char *i = vxc.data(); i < vxc.data() + vxc.size(); ++i) {
                char c = *i;
                if (c != '\\') instr.push_back(static_cast<std::byte>(c));
                else if (i < vxc.data() + vxc.size() - 1) {
                    instr.push_back(static_cast<std::byte>(resolveStr(*++i)));
                } else return {
                    .errorId = Errorcodes::ExpectedTokenFoundNothing,
                    .errTxt{std::string(1, part[0].text[0])}
                };
                if (Size != 1 && instr.size() > 1) return {
                    .parsed = part, .errorId = Errorcodes::CustomError,
                    .errTxt{"strings only work with db"},
                };
            }
            st.binOut.insert(st.binOut.end(), instr.begin(), instr.end());
            continue;
        }

        auto r = solveExpr(st, part);
        if (r.errorId != Errorcodes::NoError) return {
            .parsed = part, .errorId = r.errorId, .errTxt{r.errTxt},
        };

        for (size_t b = 0; b < Size; ++b)
            st.binOut.push_back(static_cast<std::byte>((r.exprVal >> (8 * b)) & 0xFF));
    }

    return { .parsed = ops, .rest = ops.subspan(ops.size()) };
}

// resb 16  resd 4  (zero filled, flat binary has no .bss)
template <size_t Size>
ParserOutput reserveHandler(AssemblerState &st, std::span<const Token> ops) {
    uint64_t count;
    if (auto e = singleArg(st, ops, count); e.errorId != Errorcodes::NoError) return e;

    st.binOut.insert(st.binOut.end(), count * Size, std::byte{0});
    return { .parsed = ops, .rest = ops.subspan(ops.size()) };
}

ParserOutput orgHandler(AssemblerState &st, std::span<const Token> ops) {
    uint64_t addr;
    if (auto e = singleArg(st, ops, addr); e.errorId != Errorcodes::NoError) return e;

    if (!st.binOut.empty()) return {
        .parsed = ops,
        .errorId = Errorcodes::CustomError,
        .errTxt{"org after code has been emitted"},
    };

    st.initialOrigin = addr;
    return { .parsed = ops, .rest = ops.subspan(ops.size()) };
}

// align 16
ParserOutput alignHandler(AssemblerState &st, std::span<const Token> ops) {
    uint64_t n;
    if (auto e = singleArg(st, ops, n); e.errorId != Errorcodes::NoError) return e;

    if (n == 0 || (n & (n - 1)) != 0) return {
        .parsed = ops.subspan(1),
        .errorId = Errorcodes::CustomError,
        .errTxt{std::format("align must be a power of two, got {}", n)},
    };

    size_t pad = (n - (st.here() % n)) % n;
    st.binOut.insert(st.binOut.end(), pad, static_cast<std::byte>(kAlignFill));
    return { .parsed = ops, .rest = ops.subspan(ops.size()) };
}

ParserOutput equHandler(AssemblerState &st, std::span<const Token> ops) {
    uint64_t v;
    auto it = st.symbols.find(st.label);
    if (auto e = singleArg(st, ops, v); e.errorId != Errorcodes::NoError) return e;
    if (it->second != st.here()) return {
        .parsed = ops.subspan(1),
        .errorId = Errorcodes::CustomError,
        .errTxt{"Equ not placed at top of directive"},
    };

    st.symbols[st.label] = v;
    return { .parsed = ops, .rest = ops.subspan(ops.size()) };
}

ParserOutput ignoreHandler(AssemblerState &, std::span<const Token> ops) {
    return { .parsed = ops, .rest = ops.subspan(ops.size()) };
}

const std::unordered_map<std::string, AssemblerDirectHandler> assemblerDispatchMap {
    {"ORG",     orgHandler},
    {"ALIGN",   alignHandler},
    {"EQU",     equHandler},

    {"DB",      dataHandler<1>},
    {"DW",      dataHandler<2>},
    {"DD",      dataHandler<4>},
    {"DQ",      dataHandler<8>},

    {"RESB",    reserveHandler<1>},
    {"RESW",    reserveHandler<2>},
    {"RESD",    reserveHandler<4>},
    {"RESQ",    reserveHandler<8>},

    {"SECTION", ignoreHandler},
    {"SEGMENT", ignoreHandler},
    {"GLOBAL",  ignoreHandler},
    {"EXTERN",  ignoreHandler},
};


const std::unordered_map<std::string, AssemblerDirectHandler> organizationDispatchMap {
    {"ORG",     orgHandler},
    {"SECTION", ignoreHandler},
    {"SEGMENT", ignoreHandler},
    {"GLOBAL",  ignoreHandler},
    {"EXTERN",  ignoreHandler},
};

const std::unordered_map<std::string, Keyword> keywordMap {
    {"BYTE",  Keyword::Byte},
    {"WORD",  Keyword::Word},
    {"DWORD", Keyword::Dword},
    {"QWORD", Keyword::Qword},

    {"REL",   Keyword::Rel},
    {"REL16", Keyword::Rel8},
    {"REL32", Keyword::Rel16},
    {"ABS",   Keyword::Abs},
    {"ABS16", Keyword::Abs8},
    {"ABS32", Keyword::Abs16},
};
