#ifndef COMPILER_INSTRUCTIONS_HPP
#define COMPILER_INSTRUCTIONS_HPP

#include <compiler/assemblerDirectives.hpp>
#include <compiler/lexer.hpp>
#include <compiler/assembler.hpp>


ParserOutput MovHandler(AssemblerState &state, std::span<const Token> instr);

#endif // COMPILER_INSTRUCTIONS_HPP
