
#pragma once

#include <lewis/ir.hpp>

namespace lewis::targets::x86_64 {

struct LoadConstCode
: Instruction,
        CastableIfInstructionKind<LoadConstCode, instruction_kinds::loadConstCode> {
    LoadConstCode()
    : Instruction{instruction_kinds::loadConstCode} { }
};

} // namespace lewis::targets::x86_64

