#ifndef COMPILER_ASSEMBLERDIRECTIVES_HPP
#define COMPILER_ASSEMBLERDIRECTIVES_HPP

#include <compiler/lexer.hpp>
#include <cwchar>
#include <string_view>
#include <utilities/stringutils.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <basichumanrights.hpp>


enum class ArgKind : u8 { Reg, Lit, Mem };

struct Arg {
    ArgKind kind = ArgKind::Lit;
    u64 value = 0;
};

static constexpr u8 kNoSize = 0xFF;
static constexpr u8 kOpMov  = 0x2C;

struct AssemblerState;

enum class Errorcodes {
    NoError,
    AssemblerDirectError,
    ExpectedLabel,
    ExpectedToken,
    ExpectedTokenFoundNothing,
    ExpectedValue,
    UnknownInstruction,
    UnknownSquareInstruction,
    UndefinedSymbol,
    UnknownKeyword,
    BadKeyword,
    LocalLabelNeedsAScope,
    InstrTakesValues,
    FailedToSolveExpr,
    DivisionByZero,
    MalformedInstruction,
    MalformedInstructionDescribed,
    CustomError,
};

enum class Keyword {
    Badword,
    Byte, Word, Dword, Qword,
    Rel, Rel8, Rel16, Abs, Abs8, Abs16,
};


struct ParserOutput {
    std::span<const Token> parsed{};
    std::span<const Token> rest{};

    Errorcodes errorId = Errorcodes::NoError;
    std::vector<std::string> errTxt{};

    void moveParsed(size_t amt, std::span<const Token> original) {
        parsed = original.subspan(
            0, parsed.size() + amt
        );

        rest = original.subspan(
            parsed.size()
        );
    }
};

struct AtomicEvalReturn {
    uint64_t exprVal = 0;
    Errorcodes errorId = Errorcodes::NoError;
    std::string errTxt{};
};

using SymbolTable = std::unordered_map<std::string, size_t>;
using InstructionLineHandler = ParserOutput(*)(AssemblerState &, std::span<const Token>);
using AssemblerDirectHandler = ParserOutput(*)(AssemblerState &, std::span<const Token>);

struct AssemblerState {
    size_t initialOrigin = 0;
    size_t srcLine = 0;

    std::string scope;
    std::string label;
    std::vector<std::byte> &binOut;
    SymbolTable &symbols;

    size_t here() const { return initialOrigin + binOut.size(); }
};

static inline char resolveStr(char c) {
    switch (c) {
        case 'a': return '\a';
        case 'b': return '\b';
        case 't': return '\t';
        case 'v': return '\v';
        case 'r': return '\r';
        case 'n': return '\n';
        case '0': return '\0';
        case 'f': return '\f';
        case 'e': return 0x1B;
        default:
    }
    return c;
}

// NOTE: @note TODO: @todo @hack HACK: @fixme FIXME: VERY IMPORTANT: CHANGE TO MATCH NOP INSTRUCTION!!!!
inline constexpr uchar kAlignFill = 0x90;

extern const std::unordered_map<std::string, AssemblerDirectHandler> assemblerDispatchMap;
extern const std::unordered_map<std::string, AssemblerDirectHandler> organizationDispatchMap;
extern const std::unordered_map<std::string, Keyword> keywordMap;

AtomicEvalReturn evalAtomic(const AssemblerState &st, const Token &t);
AtomicEvalReturn solveExpr(const AssemblerState &st, std::span<const Token> t);

uint64_t assemblerValue(AssemblerState &st, const Token &t);
uint64_t parseNumber(AssemblerState &st, const Token &t);

// 1 2 4 8
template <size_t Size> ParserOutput dataHandler(AssemblerState &st, std::span<const Token> ops);
template <size_t Size> ParserOutput reserveHandler(AssemblerState &st, std::span<const Token> ops);

ParserOutput orgHandler(AssemblerState &st, std::span<const Token> ops);
ParserOutput alignHandler(AssemblerState &st, std::span<const Token> ops);
ParserOutput ignoreHandler(AssemblerState &st, std::span<const Token> ops);  // useless

#endif // COMPILER_ASSEMBLERDIRECTIVES_HPP
