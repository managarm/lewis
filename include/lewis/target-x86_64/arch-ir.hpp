// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#pragma once

#include <lewis/ir.hpp>

namespace lewis::targets::x86_64 {

namespace arch_value_kinds {
    enum : ValueKindType {
        unused = value_kinds::kindsForX86,
        registerMode,
        baseDispMemoryMode
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
        nop,
        pseudoMoveSingle,
        pseudoMoveMultiple,
        pushSave,
        popRestore,
        decrementStack,
        incrementStack,
        xchgMR,
        movMC,
        movMR,
        movRM,
        // TODO: We certainly want to drop the "WithOffset" specialization.
        //       This should probably be done after we rewrite Values and make them more useful.
        defineOffset,
        negM,
        addMR,
        andMR,
        call,
    };
}

namespace arch_branch_kinds {
    enum : BranchKindType {
        unused = instruction_kinds::kindsForX86,
        ret,
        jmp,
        jnz,
    };
}

enum OperandSize {
    null,
    dword,
    qword
};

struct RegisterMode
: Value,
        CastableIfValueKind<RegisterMode, arch_value_kinds::registerMode> {
    RegisterMode()
    : Value{arch_value_kinds::registerMode} { }

    OperandSize operandSize = OperandSize::null;
    int modeRegister = -1;
};

struct BaseDispMemoryMode
: Value,
        CastableIfValueKind<BaseDispMemoryMode, arch_value_kinds::baseDispMemoryMode> {
    BaseDispMemoryMode()
    : Value{arch_value_kinds::baseDispMemoryMode} { }

    OperandSize operandSize = OperandSize::null;
    int baseRegister = -1;
    ptrdiff_t disp = 0;
};

struct NopInstruction
: Instruction,
        CastableIfInstructionKind<NopInstruction,
                arch_instruction_kinds::nop> {
    NopInstruction()
    : Instruction{arch_instruction_kinds::nop} { }
};

struct DefineOffsetInstruction
: Instruction,
        CastableIfInstructionKind<DefineOffsetInstruction,
                arch_instruction_kinds::defineOffset> {
    DefineOffsetInstruction(Value *operand_ = nullptr)
    : Instruction{arch_instruction_kinds::defineOffset},
            result{this}, operand{this, operand_} { }

    ValueOrigin result;
    ValueUse operand;
};

struct PushSaveInstruction
: Instruction,
        CastableIfInstructionKind<PushSaveInstruction,
                arch_instruction_kinds::pushSave> {
    PushSaveInstruction(int operandRegister_ = -1)
    : Instruction{arch_instruction_kinds::pushSave}, operandRegister{operandRegister_} { }

    int operandRegister;
};

struct PopRestoreInstruction
: Instruction,
        CastableIfInstructionKind<PopRestoreInstruction,
                arch_instruction_kinds::popRestore> {
    PopRestoreInstruction(int operandRegister_ = -1)
    : Instruction{arch_instruction_kinds::popRestore}, operandRegister{operandRegister_} { }

    int operandRegister;
};

struct DecrementStackInstruction
: Instruction,
        CastableIfInstructionKind<DecrementStackInstruction,
                arch_instruction_kinds::decrementStack> {
    DecrementStackInstruction(ptrdiff_t value_)
    : Instruction{arch_instruction_kinds::decrementStack}, value{value_} { }

    ptrdiff_t value;
};

struct IncrementStackInstruction
: Instruction,
        CastableIfInstructionKind<IncrementStackInstruction,
                arch_instruction_kinds::incrementStack> {
    IncrementStackInstruction(ptrdiff_t value_)
    : Instruction{arch_instruction_kinds::incrementStack}, value{value_} { }

    ptrdiff_t value;
};

// Instruction that takes a single operand and overwrites a mode M result.
struct UnaryMOverwriteInstruction
: Instruction,
        CastableIfInstructionKind<UnaryMOverwriteInstruction,
                arch_instruction_kinds::pseudoMoveSingle,
                arch_instruction_kinds::movMR,
                arch_instruction_kinds::movRM> {
    UnaryMOverwriteInstruction(InstructionKindType kind, Value *operand_ = nullptr)
    : Instruction{kind}, result{this}, operand{this, operand_} { }

    ValueOrigin result;
    ValueUse operand;
};

// Instruction that takes a single mode M operand and replaces it by the result.
struct UnaryMInPlaceInstruction
: Instruction,
        CastableIfInstructionKind<UnaryMInPlaceInstruction, arch_instruction_kinds::negM> {
    UnaryMInPlaceInstruction(InstructionKindType kind, Value *primary_ = nullptr)
    : Instruction{kind}, result{this}, primary{this, primary_} { }

    ValueOrigin result;
    ValueUse primary;
};

struct BinaryMRInPlaceInstruction
: Instruction, CastableIfInstructionKind<BinaryMRInPlaceInstruction,
        arch_instruction_kinds::addMR,
        arch_instruction_kinds::andMR> {
    BinaryMRInPlaceInstruction(InstructionKindType kind,
            Value *primary_ = nullptr, Value *secondary_ = nullptr)
    : Instruction{kind}, result{this},
            primary{this, primary_}, secondary{this, secondary_} { }

    ValueOrigin result;
    ValueUse primary;
    ValueUse secondary;
};

struct PseudoMoveSingleInstruction
: UnaryMOverwriteInstruction,
        CastableIfInstructionKind<PseudoMoveSingleInstruction,
                arch_instruction_kinds::pseudoMoveSingle> {
    PseudoMoveSingleInstruction(Value *operand_ = nullptr)
    : UnaryMOverwriteInstruction{arch_instruction_kinds::pseudoMoveSingle, operand_} { }
};

struct PseudoMoveMultipleInstruction
: Instruction, CastableIfInstructionKind<PseudoMoveMultipleInstruction,
            arch_instruction_kinds::pseudoMoveMultiple> {
private:
    struct MovePair {
        MovePair(Instruction *inst)
        : result{inst}, operand{inst} { }

        ValueOrigin result;
        ValueUse operand;
    };

public:
    PseudoMoveMultipleInstruction(size_t arity)
    : Instruction{arch_instruction_kinds::pseudoMoveMultiple} {
        for(size_t i = 0; i < arity; i++)
            _pairs.push_back(std::make_unique<MovePair>(this));
    }

    size_t arity() {
        return _pairs.size();
    }

    ValueOrigin &result(size_t i) {
        return _pairs[i]->result;
    }
    ValueUse &operand(size_t i) {
        return _pairs[i]->operand;
    }

private:
    // TODO: This can be done without another indirection.
    std::vector<std::unique_ptr<MovePair>> _pairs;
};

// TODO: Turn this into a UnaryMOverwriteInstruction.
struct MovMCInstruction
: Instruction,
        CastableIfInstructionKind<MovMCInstruction, arch_instruction_kinds::movMC> {
    MovMCInstruction()
    : Instruction{arch_instruction_kinds::movMC}, result{this} { }

    ValueOrigin result;
    uint64_t value = 0;
};

struct MovRMInstruction
: UnaryMOverwriteInstruction,
        CastableIfInstructionKind<MovRMInstruction, arch_instruction_kinds::movRM> {
    MovRMInstruction(Value *operand_ = nullptr)
    : UnaryMOverwriteInstruction{arch_instruction_kinds::movRM, operand_} { }
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
    : Instruction{arch_instruction_kinds::xchgMR}, firstResult{this}, secondResult{this},
        firstOperand{this, first}, secondOperand{this, second} { }

    ValueOrigin firstResult;
    ValueOrigin secondResult;
    ValueUse firstOperand;
    ValueUse secondOperand;
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
: Instruction,
        CastableIfInstructionKind<CallInstruction, arch_instruction_kinds::call> {
    CallInstruction(size_t numOperands_, size_t numResults_)
    : Instruction{arch_instruction_kinds::call} {
        for (size_t i = 0; i < numOperands_; i++)
            _operands.push_back(std::make_unique<ValueUse>(this));
        for (size_t i = 0; i < numResults_; i++)
            _results.push_back(std::make_unique<ValueOrigin>(this));
    }

    std::string function;

    size_t numOperands() { return _operands.size(); }
    ValueUse &operand(size_t i) { return *_operands[i]; }

    size_t numResults() { return _results.size(); }
    ValueOrigin &result(size_t i) { return *_results[i]; }

private:
    // TODO: This can be done without another indirection.
    std::vector<std::unique_ptr<ValueUse>> _operands;
    std::vector<std::unique_ptr<ValueOrigin>> _results;
};

struct RetBranch
: Branch,
        CastableIfBranchKind<RetBranch, arch_branch_kinds::ret> {
    RetBranch(size_t numOperands_)
    : Branch{arch_branch_kinds::ret} {
        for (size_t i = 0; i < numOperands_; i++)
            _operands.push_back(std::make_unique<ValueUse>(nullptr));
    }

    size_t numOperands() { return _operands.size(); }
    ValueUse &operand(size_t i) { return *_operands[i]; }

private:
    // TODO: This can be done without another indirection.
    std::vector<std::unique_ptr<ValueUse>> _operands;
};

struct JmpBranch
: Branch,
        CastableIfBranchKind<JmpBranch, arch_branch_kinds::jmp> {
    JmpBranch(BasicBlock *target_ = nullptr)
    : Branch{arch_branch_kinds::jmp}, target{target_} { }

    // TODO: Use a BlockLink class similar to ValueUse.
    BasicBlock *target;
};

struct JnzBranch
: Branch,
        CastableIfBranchKind<JnzBranch, arch_branch_kinds::jnz> {
    JnzBranch(BasicBlock *ifTarget_ = nullptr, BasicBlock *elseTarget_ = nullptr)
    : Branch{arch_branch_kinds::jnz}, ifTarget{ifTarget_}, elseTarget{elseTarget_},
            operand{nullptr} { }

    // TODO: Use a BlockLink class similar to ValueUse.
    BasicBlock *ifTarget;
    BasicBlock *elseTarget;
    ValueUse operand;
};

} // namespace lewis::targets::x86_64
