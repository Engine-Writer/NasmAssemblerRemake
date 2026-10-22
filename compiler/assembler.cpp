#include "ctre/ctre.hpp"
#include "utilities/stringutils.hpp"
#include <cstddef>
#include <exception>
#include <memory>
#include <string_view>
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>
#include <string>
#include <iostream>
#include <span>
#include <sstream>
#include <istream>
#include <ostream>
#include <format>

#include <basichumanrights.hpp>
#include <compiler/instructions.hpp>
#include <compiler/lexer.hpp>
#include <compiler/assembler.hpp>
#include <compiler/assemblerDirectives.hpp>
#include <compiler/preprocessor.hpp>

static std::string formatError(
    const AssemblerState &state,
    std::span<const Token> toks,
    const ParserOutput &out,
    size_t srcLine,
    std::string_view origLine
) {
    auto txt = [&](size_t i) { return i < out.errTxt.size() ? out.errTxt[i] : std::string{}; };
    std::string_view first = out.rest.empty() ? std::string_view{} : out.rest[0].text;

    std::string msg;
        switch (out.errorId) {
        case Errorcodes::NoError:
            msg = "no error";
            break;
        case Errorcodes::AssemblerDirectError:
            msg = "assembler directive failed";
            break;
        case Errorcodes::ExpectedLabel:
            msg = std::format("expected a label or directive after `{}`", first);
            break;
        case Errorcodes::ExpectedToken:
            msg = std::format("expected token `{}`, got: `{}`", txt(0), txt(1));
            break;
        case Errorcodes::ExpectedTokenFoundNothing:
            msg = std::format("expected token `{}`, but found nothing", txt(0));
            break;
        case Errorcodes::ExpectedValue:
            msg = std::format("expected a value, got: `{}`", txt(0));
            break;
        case Errorcodes::UnknownInstruction:
            msg = std::format("unknown instruction `{}` does not exist", first);
            break;
        case Errorcodes::UnknownSquareInstruction:
            msg = std::format("unknown instruction `[{}]` does not exist", first);
            break;
        case Errorcodes::UndefinedSymbol:
            msg = std::format("undefined symbol: `{}`", txt(0));
            break;
        case Errorcodes::UnknownKeyword:
            msg = std::format("unknown keyword `{}` does not exist", txt(0));
            break;
        case Errorcodes::BadKeyword:
            msg = std::format(
                "keyword `{}` is invalid for instruction `{}`",
                txt(0), txt(1)
            );
            break;
        case Errorcodes::LocalLabelNeedsAScope:
            msg = std::format("local label `{}` needs a scope", first);
            break;
        case Errorcodes::InstrTakesValues:
            msg = std::format(
                "instruction `{}` takes {} values, but {} were given",
                first, txt(0), txt(1)
            );
            break;
        case Errorcodes::FailedToSolveExpr:
            msg = std::format("failed to solve expression: `{}`", txt(0));
            break;
        case Errorcodes::DivisionByZero:
            msg = std::format("division by zero");
            break;
        case Errorcodes::MalformedInstruction:
            msg = std::format("malformed instruction");
            break;
        case Errorcodes::MalformedInstructionDescribed:
            msg = std::format("malformed instruction: {}", txt(0));
            break;
        case Errorcodes::CustomError:
            msg = std::format("error: {}", txt(0));
            break;
        default:
            msg = std::format("unknown error {}", static_cast<int>(out.errorId) & 0xFF);
            break;
    }

    return std::format(
        "line {}: {}\n    {}\n",
        srcLine, msg, origLine
    );
}

using SymbolTable = std::unordered_map<std::string, size_t>;
using InstructionLineHandler = ParserOutput(*)(AssemblerState &, std::span<const Token>);
using AssemblerDirectHandler = ParserOutput(*)(AssemblerState &, std::span<const Token>);

static constexpr int kMaxPasses = 32;
static const std::unordered_map<std::string, InstructionLineHandler> instructionDispatchMap {
    {"MOV", MovHandler}
};

std::unordered_map<std::string, size_t> addressOffset{};


static inline Errorcodes labelHelper(std::string tempStr, AssemblerState &state) {
    if (tempStr[0] == '$') tempStr.erase(0, 1);
    if (tempStr.starts_with("..")) /* nop */;
    else if (tempStr.starts_with(".")) {
        if (tempStr[0] == '.' && state.scope.empty()) return Errorcodes::LocalLabelNeedsAScope;
        tempStr = state.scope + tempStr;
    } else state.scope = tempStr;

    state.label = tempStr;
    state.symbols[tempStr] = state.here();

    return Errorcodes::NoError;
}

static ParserOutput parseLabel(
    AssemblerState &state, std::span<const Token> toks, bool computeLabels = false
) noexcept {
    if (toks.size() < 3) return { .errorId = Errorcodes::ExpectedLabel };
    if (toks[0].kind != Tok::Ident) return { .errorId = Errorcodes::ExpectedLabel };

    if (toks[1].kind == Tok::Colon) { 
        auto tempStr(std::string(toks[0].text));
        auto error = labelHelper(tempStr, state);
        if (error != Errorcodes::NoError) return { .errorId = error };
        return {
            .parsed = toks.first(2),
            .rest = toks.subspan(2),
        };
    }

    auto it = assemblerDispatchMap.find(capitalizeStr(std::string(toks[1].text)));
    if (toks[1].kind == Tok::Ident && it != assemblerDispatchMap.end() && !computeLabels) {
        auto tempStr(std::string(toks[0].text));
        auto error = labelHelper(tempStr, state);
        if (error != Errorcodes::NoError) return { .errorId = error };

        auto args = toks.first(toks.size() - 1).subspan(1);
        ParserOutput myParsed = it->second(state, args);
        if (myParsed.errorId != Errorcodes::NoError) return myParsed;
        return {
            .parsed = toks,
            .rest = toks.subspan(toks.size()),
        };
    } else if (toks[1].kind == Tok::Ident && it != assemblerDispatchMap.end()) {
        auto tempStr(std::string(toks[0].text));
        auto error = labelHelper(tempStr, state);
        if (error != Errorcodes::NoError) return { .errorId = error };

        return {
            .parsed = toks,
            .rest = toks.subspan(toks.size()),
        };
    }

    return { .errorId = Errorcodes::ExpectedLabel };
}

static inline ParserOutput parseInstr(AssemblerState &state, std::span<const Token> tokIs) {
    auto toks = !tokIs.empty() && tokIs.back().kind == Tok::End ?
        tokIs.first(tokIs.size() - 1) : tokIs;

    if (toks.size() >= 3 && toks[0].kind == Tok::LBracket && getIsFullSquareBPURE(toks)) {
        auto subspO = getTokenSpanInsideSquareBracket(toks);
        if (!subspO.has_value()) return { .errorId = Errorcodes::MalformedInstruction };
        auto subsp = subspO.value();

        auto instruction(std::string(subsp[0].text));
        auto it = organizationDispatchMap.find(capitalizeStr(std::string(subsp[0].text)));
        if (it == organizationDispatchMap.end()) return {
            .rest = subsp,
            .errorId = Errorcodes::UnknownSquareInstruction,
        };

        return it->second(state, subsp);

    } else if (toks.size() >= 3 && toks[0].kind == Tok::LBracket) return {
        .errorId = Errorcodes::MalformedInstruction
    };

    if (toks.size() == 0) return { 
        .errorId = Errorcodes::CustomError,
        .errTxt{"Fix assembler bro"}
    };
    auto instruction(capitalizeStr(std::string(toks[0].text)));
    auto itIDM = instructionDispatchMap.find(instruction);
    if (itIDM != instructionDispatchMap.end()) {
        return itIDM->second(state, toks);
    }

    auto itADM = assemblerDispatchMap.find(instruction);
    if (itADM != assemblerDispatchMap.end()) {
        return itADM->second(state, toks);
    }

    return {
        .errorId = Errorcodes::UnknownInstruction
    };
}

static inline ParserOutput parseLine(AssemblerState &state, std::span<const Token> toks) {
    ParserOutput parserStateOp{};
    ParserOutput parserState{
        .parsed = toks.first(0),
        .rest = toks,
    };
    if (parserState.rest.empty() || parserState.rest[0].kind == Tok::End) return parserState;

    while (isLabel(parserState.rest)) {
        parserStateOp = parseLabel(state, parserState.rest);
        if (parserStateOp.errorId != Errorcodes::NoError) return {
            .rest = parserStateOp.rest.empty() ? parserState.rest : parserStateOp.rest,
            .errorId = parserStateOp.errorId,
            .errTxt = std::move(parserStateOp.errTxt),
        };
        parserState.moveParsed(parserStateOp.parsed.size(), toks);
    }
    if (parserState.rest.empty() || parserState.rest[0].kind == Tok::End) return parserState;

    parserStateOp = parseInstr(state, parserState.rest);
    if (parserStateOp.errorId != Errorcodes::NoError) return {
        .rest = parserStateOp.rest.empty() ? parserState.rest : parserStateOp.rest,
        .errorId = parserStateOp.errorId,
        .errTxt = std::move(parserStateOp.errTxt),
    };
    parserState.moveParsed(parserStateOp.parsed.size(), toks);

    return parserState;
}


Errorable<std::vector<std::byte>> assemblerMain(std::string &inFile) {
    // step -1: Solve macaroo
    Preprocessor preprocessor;
    auto pre = preprocessor.run(inFile);
    if (pre.error) return {
        .main{}, .error = 0x100 | pre.error,
        .errTxt = std::format("preprocessor error: {}\n", pre.errTxt),
    };

    const Expanded &preProcOut = pre.main;
    Errorable<std::vector<std::vector<Token>>> linesLex;

    // I am NOT writing a whole type, for a singular function. naaaaah bro go fys
    linesLex = lexLuthorLines(preProcOut.text);
    if (linesLex.error) return {
        .main{}, .error = 0x200 | linesLex.error,
        .errTxt = std::format("lexer error: {}\n", linesLex.errTxt),
    };
    
    std::vector<std::byte> bytearray{};

    AssemblerState state{
        .scope{}, .label{},
        .binOut = bytearray,
        .symbols = addressOffset,
    };

    // step 1: find all labels and make sure no dupes
    for (const auto &line : linesLex.main) {
        std::span<const Token> rest(line);
        size_t srcLine = preProcOut.lineMap[line[0].line - 1];

        while (isLabel(rest)) {
            std::string name(rest[0].text);

            if (weMayContain(state, name)) return { 
                .main{}, .error = 1,
                .errTxt = std::format("line {}: label redefined: {}\n", srcLine, name)
            };

            ParserOutput x = parseLabel(state, rest, true);
            if (x.errorId != Errorcodes::NoError) return { 
                .main{}, .error = 1,
                .errTxt = formatError(state, line, x, srcLine, getLine(inFile, srcLine))
            };
            rest = rest.subspan(x.parsed.size());
        }
    }

    // step 2: solve the binary until you manage to use the smallest instr possible
    bool resolved = false;
    bool buildFailed = false;
    for (int i=0; i<kMaxPasses && !resolved; ++i) {
        state.scope.clear();
        state.label.clear();
        state.binOut.clear();
        auto oldAddrOffset(addressOffset);

        for (const auto &ln : linesLex.main) {
            std::span<const Token> tkSpan(ln);
            ParserOutput parOut(parseLine(state, tkSpan));
            if (parOut.errorId != Errorcodes::NoError) {
                size_t srcLine = preProcOut.lineMap[ln[0].line - 1];
                std::cerr << formatError(
                    state, tkSpan, parOut, srcLine, getLine(inFile, srcLine)
                );
                buildFailed = true;
            }
        }
        if (buildFailed) return {
            .main{}, .error = 1,
            .errTxt = "one or more build errors occured. stopping now\n",
        };
        if (oldAddrOffset == addressOffset) resolved = true;
    }

    // step 3: debug binary for 4hrs and then come back here and blame the assembler
    return { .main = std::move(bytearray) };
}
