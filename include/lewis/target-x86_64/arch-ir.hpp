// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#pragma once

#include <lewis/ir.hpp>

namespace lewis::targets::x86_64 {

namespace arch_value_kinds {
    enum : ValueKindType {
        unused = value_kinds::kindsForX86,
        modeMResult
    };
}

namespace arch_instruction_kinds {
    // Convention: x86 instructions follow the naming scheme <opcode>_<operands>.
    // Operands are encoded as single letters, as follows:
    // M: Register or memory reference (ModRM + SIB).
    // R: Register reference.
    // C: Immediate constant.
    enum : InstructionKindType {
        unused = instruction_kinds::kindsForX86,
        movMC,
        movMR,
        negM,
    };
}

struct ModeMResult
: Value,
        CastableIfValueKind<ModeMResult, value_kinds::genericResult> {
    ModeMResult()
    : Value{value_kinds::genericResult} { }

    int modeRegister = 0;
};

struct WithModeMResult {
    ModeMResult *result() {
        return &_result;
    }

private:
    ModeMResult _result;
};

struct MovMCInstruction
: Instruction, WithModeMResult,
        CastableIfInstructionKind<MovMCInstruction, arch_instruction_kinds::movMC> {
    MovMCInstruction()
    : Instruction{arch_instruction_kinds::movMC} { }

    uint64_t value = 0;
};

struct MovMRInstruction
: Instruction, WithModeMResult,
        CastableIfInstructionKind<MovMRInstruction, arch_instruction_kinds::movMR> {
    MovMRInstruction(Value *operand_ = nullptr)
    : Instruction{arch_instruction_kinds::movMR}, operand{operand_} { }

    ValueUse operand;
};

struct NegMInstruction
: Instruction, WithModeMResult,
        CastableIfInstructionKind<NegMInstruction, arch_instruction_kinds::negM> {
    NegMInstruction(Value *operand_ = nullptr)
    : Instruction{arch_instruction_kinds::negM}, operand{operand_} { }

    ValueUse operand;
};

} // namespace lewis::targets::x86_64