#include "utilities/stringutils.hpp"
#include <cstddef>
#include <memory>
#include <string_view>
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>
#include <string>
#include <iostream>
#include <span>
#include <format>

#include <basichumanrights.hpp>
#include <compiler/instructions.hpp>
#include <compiler/lexer.hpp>
#include <compiler/assembler.hpp>
#include <compiler/assemblerDirectives.hpp>
#include <compiler/preprocessor.hpp>


ParserOutput MovHandler(AssemblerState &state, std::span<const Token> instr) {
    std::optional<Keyword> kw;
    size_t i = 1;
    if (i < instr.size()) {
        auto it = keywordMap.find(capitalizeStr(std::string(instr[i].text)));
        if (it != keywordMap.end()) { kw = it->second; ++i; }
    }

    u8 sizeCode = kw ? getSize(*kw) : kNoSize;
    if (kw && sizeCode == kNoSize) return {
        .errorId = Errorcodes::BadKeyword,
        .errTxt{std::string(instr[1].text), std::string(instr[0].text)},
    };

    std::vector<Arg> args;
    for (auto rest = instr.subspan(i); !rest.empty(); ) {
        auto part = getEndofArgument(rest);
        Arg a;
        auto e = makeArg(state, part, a);
        if (e.errorId != Errorcodes::NoError) return e;
        args.push_back(a);

        // fun fact: the following line is cursed and allows cool non-standard shit
        // now i could fix but highkey 10/10 bug looks nice asf and might be better
        // than the real nasm ig XD
        rest = rest.subspan(std::min(rest.size(), part.size() + 1));
    }

    if (args.size() != 2) return {
        .errorId = Errorcodes::InstrTakesValues,
        .errTxt{"2", std::to_string(args.size())},
    };
    if (args[0].kind != ArgKind::Reg) return {
        .errorId = Errorcodes::ExpectedToken,
        .errTxt{"expected register"},
    };
    if (args[1].kind == ArgKind::Mem) return {
        .errorId = Errorcodes::CustomError,
        .errTxt{"mov instruction cant take address check out load/store"},
    };

    if (sizeCode == kNoSize) sizeCode = getSize(sizeOfReg(args[0].value));
    if (sizeCode == kNoSize) return {
        .errorId = Errorcodes::MalformedInstructionDescribed,
        .errTxt{"Bad codegen"},
    };

    if (args[1].kind == ArgKind::Lit && !valFits(args[1].value, sizeCode)) return {
        .errorId = Errorcodes::CustomError,
        .errTxt{std::format(
            "value {} does not fit in {} byte(s)",
            static_cast<long long>(args[1].value), sizeBytes(sizeCode)
        )},
    };

    writeByte(state, static_cast<u8>(kOpMov | (args[1].kind == ArgKind::Reg)));
    writeByte(state, sizeCode);
    writeArg(state, args[0], sizeCode);
    writeArg(state, args[1], sizeCode);

    return { .parsed = instr, .rest = instr.subspan(instr.size()) };
}
