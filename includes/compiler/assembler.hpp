#ifndef COMPILER_ASSEMBLER_HPP
#define COMPILER_ASSEMBLER_HPP

#include "compiler/assemblerDirectives.hpp"
#include "compiler/lexer.hpp"
#include <cctype>
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>
#include <string>
#include <charconv>
#include <cstdint>

#include <ctre/ctre.hpp>
#include <basichumanrights.hpp>

using uchar = unsigned char;
Errorable<std::vector<std::byte>> assemblerMain(std::string &inFile);

static inline Errorable<std::string> labelLUTName(const AssemblerState &state, std::string label) {
    if (label[0] == '$') label.erase(0, 1);
    if (label.starts_with("..")) return { .main = label };
    else if (label.starts_with(".") && !state.scope.empty()) return {
        .main = state.scope + label
    };
    else if (label.starts_with(".")) return {
        .main = "",
        .error = static_cast<int>(Errorcodes::LocalLabelNeedsAScope)
    };
    else return { .main = label };
}

static inline bool weMayContain(const AssemblerState &state, std::string label) {
    if (label[0] == '$') label.erase(0, 1);
    if (label.starts_with("..")) return state.symbols.find(label) != state.symbols.end();
    else if (label.starts_with("."))
        return state.symbols.find(state.scope + label) != state.symbols.end();
    else return state.symbols.find(label) != state.symbols.end();
}

static inline bool isLabel(std::span<const Token> toks) {
    if (toks.size() < 3) return false;
    if (toks[0].kind != Tok::Ident) return false;

    if (toks[1].kind == Tok::Colon) return true;

    auto it = assemblerDispatchMap.find(capitalizeStr(std::string(toks[1].text)));
    if (toks[1].kind == Tok::Ident && it != assemblerDispatchMap.end()) return true;

    return false;
}

static inline std::span<const Token> getEndofArgument(std::span<const Token> toks) {
    size_t ctr = 0;
    while (toks.size() > ctr && toks[ctr].kind != Tok::Comma && toks[ctr].kind != Tok::End)
        ++ctr;
    return toks.first(ctr > toks.size() ? toks.size() : ctr);
}

static inline AtomicEvalReturn strMakeReg(std::string_view v) {
    if (v.empty()) return {
        .errorId = Errorcodes::MalformedInstruction
    };

    u8 regMode = 0;
    switch (v[0]) {
        case 'r': case 'R': regMode = 0; break;
        case 'e': case 'E': regMode = 1; break;
        case 'w': case 'W': regMode = 2; break;
        case 'l': case 'L': regMode = 3; break;
        case 'h': case 'H': regMode = 4; break;
        default: return {
            .errorId = Errorcodes::MalformedInstruction,
        };
    }

    auto s(v.substr(1));
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

    uint64_t r = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), r, base);
    if (ec != std::errc{} || p != s.data() + s.size()) return {
        .errorId = Errorcodes::FailedToSolveExpr,
        .errTxt{"could not resolve number"}
    };
    if (r > 31) {
        return {
            .errorId = Errorcodes::MalformedInstructionDescribed,
            .errTxt{"register index must be between 0 and 31"}
        };
    }

    return { .exprVal = r | (uint64_t(regMode) << 5) };
}


static inline Keyword sizeOfReg(u8 regByte) {
    switch (regByte >> 5) {
        case 0: return Keyword::Qword;
        case 1: return Keyword::Dword;
        case 2: return Keyword::Word;
        case 3: return Keyword::Byte;
        case 4: return Keyword::Byte;
        default: return Keyword::Badword;
    }
}

static u8 getSize(Keyword k) {
    switch (k) {
        case Keyword::Byte:  return 3;
        case Keyword::Word:  return 2;
        case Keyword::Dword: return 1;
        case Keyword::Qword: return 0;
        default:             return kNoSize;
    }
}

static size_t sizeBytes(u8 code) { return 1 << (3 - code); }

static bool valFits(u64 v, u8 code) {
    size_t bits = 8 * sizeBytes(code);
    if (bits == 64) return true;
    u64 hi = v >> bits;
    return hi == 0 || (hi == (~u64(0) >> bits) && ((v >> (bits - 1)) & 1));
}

template <std::integral T>
static Keyword valSize(T v) {
    if constexpr (std::is_signed_v<T>) {
        if (v >= -0x80 && v <= 0x7F) return Keyword::Byte;
        if (v >= -0x8000 && v <= 0x7FFF) return Keyword::Word;
        if (v >= -0x8000'0000 && v <= 0x7FFF'FFFF) return Keyword::Dword;
        return Keyword::Qword;
    } else {
        if (v <= 0xFF) return Keyword::Byte;
        if (v <= 0xFFFF) return Keyword::Word;
        if (v <= 0xFFFF'FFFF) return Keyword::Dword;
        return Keyword::Qword;

    }
}

static void writeByte(AssemblerState &st, u8 b) {
    st.binOut.push_back(static_cast<std::byte>(b));
}

static void writeArg(AssemblerState &st, const Arg &a, u8 code) {
    size_t n = a.kind == ArgKind::Reg ? 1 : sizeBytes(code);
    for (size_t b = 0; b < n; ++b) writeByte(st, static_cast<u8>(a.value >> (8 * b)));
}

static ParserOutput makeArg(AssemblerState &st, std::span<const Token> t, Arg &out) {
    if (t.empty()) return {
        .errorId = Errorcodes::ExpectedToken,
        .errTxt{"expected argument"},
    };

    if (getIsFullSquareBPURE(t)) {
        out = { ArgKind::Mem, 0 };
        return {};
    }

    if (t.size() == 1) {
        auto reg = strMakeReg(t[0].text);
        if (reg.errorId == Errorcodes::NoError) {
            out = { ArgKind::Reg, reg.exprVal }; return {};
        }
    }

    auto lit = solveExpr(st, t);
    if (lit.errorId != Errorcodes::NoError) return {
        .parsed = t,
        .errorId = lit.errorId,
        .errTxt{lit.errTxt},
    };
    out = { ArgKind::Lit, lit.exprVal };
    return {};
}


#endif // COMPILER_ASSEMBLER_HPP
