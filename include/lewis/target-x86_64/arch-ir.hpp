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
        pseudoMovMR,
        xchgMR,
        movMC,
        movMR,
        negM,
        addMR,
        andMR,
        call,
    };
}

namespace arch_phi_kinds {
    enum : PhiKindType {
        unused = phi_kinds::kindsForX86,
        modeM
    };
}

namespace arch_branch_kinds {
    enum : BranchKindType {
        unused = instruction_kinds::kindsForX86,
        ret,
        jmp,
    };
}

struct ModeMResult
: Value,
        CastableIfValueKind<ModeMResult, arch_value_kinds::modeMResult> {
    ModeMResult()
    : Value{arch_value_kinds::modeMResult} { }

    int modeRegister = -1;
};

struct ModeMPhiNode
: PhiNode,
        CastableIfPhiKind<ModeMPhiNode, arch_phi_kinds::modeM> {
    ModeMPhiNode()
    : PhiNode{arch_phi_kinds::modeM} { }

    int modeRegister = -1;
};

struct WithModeMResult {
    ModeMResult *result() {
        return &_result;
    }

private:
    ModeMResult _result;
};

// Instruction that takes a single operand and overwrites a mode M result.
struct UnaryMOverwriteInstruction
: Instruction, WithModeMResult,
        CastableIfInstructionKind<UnaryMOverwriteInstruction,
                arch_instruction_kinds::pseudoMovMR,
                arch_instruction_kinds::movMR> {
    UnaryMOverwriteInstruction(InstructionKindType kind, Value *operand_ = nullptr)
    : Instruction{kind}, operand{this, operand_} { }

    ValueUse operand;
};

// Instruction that takes a single mode M operand and replaces it by the result.
struct UnaryMInPlaceInstruction
: Instruction, WithModeMResult,
        CastableIfInstructionKind<UnaryMInPlaceInstruction, arch_instruction_kinds::negM> {
    UnaryMInPlaceInstruction(InstructionKindType kind, Value *primary_ = nullptr)
    : Instruction{kind}, primary{this, primary_} { }

    ValueUse primary;
};

struct BinaryMRInPlaceInstruction
: Instruction, WithModeMResult, CastableIfInstructionKind<BinaryMRInPlaceInstruction,
        arch_instruction_kinds::addMR,
        arch_instruction_kinds::andMR> {
    BinaryMRInPlaceInstruction(InstructionKindType kind,
            Value *primary_ = nullptr, Value *secondary_ = nullptr)
    : Instruction{kind}, primary{this, primary_}, secondary{this, secondary_} { }

    ValueUse primary;
    ValueUse secondary;
};

struct PseudoMovMRInstruction
: UnaryMOverwriteInstruction,
        CastableIfInstructionKind<PseudoMovMRInstruction, arch_instruction_kinds::pseudoMovMR> {
    PseudoMovMRInstruction(Value *operand_ = nullptr)
    : UnaryMOverwriteInstruction{arch_instruction_kinds::pseudoMovMR, operand_} { }
};

// TODO: Turn this into a UnaryMOverwriteInstruction.
struct MovMCInstruction
: Instruction, WithModeMResult,
        CastableIfInstructionKind<MovMCInstruction, arch_instruction_kinds::movMC> {
    MovMCInstruction()
    : Instruction{arch_instruction_kinds::movMC} { }

    uint64_t value = 0;
};

struct MovMRInstruction
: UnaryMOverwriteInstruction,
        CastableIfInstructionKind<MovMRInstruction, arch_instruction_kinds::movMR> {
    MovMRInstruction(Value *operand_ = nullptr)
    : UnaryMOverwriteInstruction{arch_instruction_kinds::movMR, operand_} { }
};

struct XchgMRInstruction
: Instruction,
        CastableIfInstructionKind<XchgMRInstruction, arch_instruction_kinds::xchgMR>{

    XchgMRInstruction(Value *first = nullptr, Value *second = nullptr)
    : Instruction{arch_instruction_kinds::xchgMR},
        firstOperand{this, first}, secondOperand{this, second} { }

    ModeMResult *firstResult() {
        return &_firstResult;
    }

    ModeMResult *secondResult() {
        return &_secondResult;
    }

    ValueUse firstOperand;
    ValueUse secondOperand;

private:
    ModeMResult _firstResult;
    ModeMResult _secondResult;
};

struct NegMInstruction
: UnaryMInPlaceInstruction,
        CastableIfInstructionKind<NegMInstruction, arch_instruction_kinds::negM> {
    NegMInstruction(Value *primary_ = nullptr)
    : UnaryMInPlaceInstruction{arch_instruction_kinds::negM, primary_} { }
};

struct AddMRInstruction
: BinaryMRInPlaceInstruction,
        CastableIfInstructionKind<AddMRInstruction, arch_instruction_kinds::addMR> {
    AddMRInstruction(Value *primary_ = nullptr, Value *secondary_ = nullptr)
    : BinaryMRInPlaceInstruction{arch_instruction_kinds::addMR, primary_, secondary_} { }
};

struct AndMRInstruction
: BinaryMRInPlaceInstruction,
        CastableIfInstructionKind<AndMRInstruction, arch_instruction_kinds::andMR> {
    AndMRInstruction(Value *primary_ = nullptr, Value *secondary_ = nullptr)
    : BinaryMRInPlaceInstruction{arch_instruction_kinds::andMR, primary_, secondary_} { }
};

struct CallInstruction
: Instruction, WithModeMResult,
        CastableIfInstructionKind<CallInstruction, arch_instruction_kinds::call> {
    CallInstruction()
    : Instruction{arch_instruction_kinds::call} { }

    std::string function;
};

struct RetBranch
: Branch,
        CastableIfBranchKind<RetBranch, arch_branch_kinds::ret> {
    RetBranch()
    : Branch{arch_branch_kinds::ret} { }
};

struct JmpBranch
: Branch,
        CastableIfBranchKind<JmpBranch, arch_branch_kinds::jmp> {
    JmpBranch(BasicBlock *target_ = nullptr)
    : Branch{arch_branch_kinds::jmp}, target{target_} { }

    // TODO: Use a BlockLink class similar to ValueUse.
    BasicBlock *target;
};

} // namespace lewis::targets::x86_64
