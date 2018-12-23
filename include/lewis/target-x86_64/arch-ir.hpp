
#pragma once

#include <lewis/ir.hpp>

namespace lewis::targets::x86_64 {

namespace arch_instruction_kinds {
    // Convention: x86 instructions follow the naming scheme <opcode>_<operands>.
    // Operands are encoded as single letters, as follows:
    // M: Register or memory reference (ModRM + SIB).
    // C: Immediate constant.
    enum : InstructionKindType {
        unused = instruction_kinds::kindsForX86,
        movMC,
    };
}

struct MovMCInstruction
: Instruction,
        CastableIfInstructionKind<MovMCInstruction, arch_instruction_kinds::movMC> {
    MovMCInstruction()
    : Instruction{arch_instruction_kinds::movMC} { }

    uint64_t value = 0;
};

} // namespace lewis::targets::x86_64

