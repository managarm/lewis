// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>
#include <lewis/target-x86_64/arch-ir.hpp>
#include <lewis/target-x86_64/arch-passes.hpp>

namespace lewis::targets::x86_64 {

struct LowerCodeImpl : LowerCodePass {
    LowerCodeImpl(BasicBlock *bb)
    : _bb{bb} { }

    void run() override;

private:
    BasicBlock *_bb;
};

void LowerCodeImpl::run() {
    auto lowerValue = [] (Value *value) {
        auto localValue = hierarchy_cast<LocalValue *>(value);
        assert(localValue);
        auto lower = std::make_unique<RegisterMode>();
        if (localValue->getType()->typeKind == type_kinds::pointer) {
            lower->operandSize = OperandSize::qword;
        } else if (localValue->getType()->typeKind == type_kinds::int32) {
            lower->operandSize = OperandSize::dword;
        } else if (localValue->getType()->typeKind == type_kinds::int64) {
            lower->operandSize = OperandSize::qword;
        } else {
            assert(!"Unexpected type kind");
        }
        return lower;
    };

    auto lowerValueWithOffset = [] (Value *value, ptrdiff_t offset) {
        auto localValue = hierarchy_cast<LocalValue *>(value);
        assert(localValue);
        auto lower = std::make_unique<BaseDispMemoryMode>();
        if (localValue->getType()->typeKind == type_kinds::pointer) {
            lower->operandSize = OperandSize::qword;
        } else if (localValue->getType()->typeKind == type_kinds::int32) {
            lower->operandSize = OperandSize::dword;
        } else if (localValue->getType()->typeKind == type_kinds::int64) {
            lower->operandSize = OperandSize::qword;
        } else {
            assert(!"Unexpected type kind");
        }
        lower->disp = offset;
        return lower;
    };

    for (auto it = _bb->phis().begin(); it != _bb->phis().end(); ++it) {
        auto lowerPhi = lowerValue((*it)->value.get());
        (*it)->value.get()->replaceAllUses(lowerPhi.get());
        (*it)->value.set(std::move(lowerPhi));
    }

    for (auto it = _bb->instructions().begin(); it != _bb->instructions().end(); ++it) {
        if (auto loadConst = hierarchy_cast<LoadConstInstruction *>(*it); loadConst) {
            auto lower = std::make_unique<MovMCInstruction>();
            auto lowerResult = lower->result.set(lowerValue(loadConst->result.get()));
            lower->value = loadConst->value;
            loadConst->result.get()->replaceAllUses(lowerResult);

            it = _bb->replaceInstruction(it, std::move(lower));
        } else if (auto loadOffset = hierarchy_cast<LoadOffsetInstruction *>(*it); loadOffset) {
            auto lowerOffset = std::make_unique<DefineOffsetInstruction>(loadOffset->operand.get());
            auto offsetValue = lowerOffset->result.set(lowerValueWithOffset(
                    loadOffset->result.get(), loadOffset->offset));

            auto lowerMov = std::make_unique<MovRMInstruction>(offsetValue);
            auto resultValue = lowerMov->result.set(lowerValue(loadOffset->result.get()));
            loadOffset->result.get()->replaceAllUses(resultValue);

            loadOffset->operand = nullptr;
            it = _bb->replaceInstruction(it, std::move(lowerOffset));
            auto nit = it;
            ++nit;
            _bb->insertInstruction(nit, std::move(lowerMov));
            ++it;
        } else if (auto unaryMath = hierarchy_cast<UnaryMathInstruction *>(*it); unaryMath) {
            std::unique_ptr<UnaryMInPlaceInstruction> lower;
            if (unaryMath->opcode == UnaryMathOpcode::negate) {
                lower = std::make_unique<NegMInstruction>();
            } else {
                assert(!"Unexpected unary math opcode");
            }
            auto lowerResult = lower->result.set(lowerValue(unaryMath->result.get()));
            lower->primary = unaryMath->operand.get();
            unaryMath->result.get()->replaceAllUses(lowerResult);

            unaryMath->operand = nullptr;
            it = _bb->replaceInstruction(it, std::move(lower));
        } else if (auto binaryMath = hierarchy_cast<BinaryMathInstruction *>(*it); binaryMath) {
            std::unique_ptr<BinaryMRInPlaceInstruction> lower;
            if (binaryMath->opcode == BinaryMathOpcode::add) {
                lower = std::make_unique<AddMRInstruction>();
            } else if (binaryMath->opcode == BinaryMathOpcode::bitwiseAnd) {
                lower = std::make_unique<AndMRInstruction>();
            } else {
                assert(!"Unexpected binary math opcode");
            }
            auto lowerResult = lower->result.set(lowerValue(binaryMath->result.get()));
            lower->primary = binaryMath->left.get();
            lower->secondary = binaryMath->right.get();
            binaryMath->result.get()->replaceAllUses(lowerResult);

            binaryMath->left = nullptr;
            binaryMath->right = nullptr;
            it = _bb->replaceInstruction(it, std::move(lower));
        } else if (auto invoke = hierarchy_cast<InvokeInstruction *>(*it); invoke) {
            auto lower = std::make_unique<CallInstruction>(invoke->numOperands());
            lower->function = invoke->function;

            auto lowerResult = lower->result.set(lowerValue(invoke->result.get()));
            invoke->result.get()->replaceAllUses(lowerResult);

            for (size_t i = 0; i < invoke->numOperands(); ++i) {
                lower->operand(i) = invoke->operand(i).get();
                invoke->operand(i) = nullptr;
            }

            it = _bb->replaceInstruction(it, std::move(lower));
        } else {
            assert(!"Unexpected generic IR instruction");
        }
    }

    auto branch = _bb->branch();
    if (auto functionReturn = hierarchy_cast<FunctionReturnBranch *>(branch); functionReturn) {
        auto lower = std::make_unique<RetBranch>(functionReturn->numOperands());

        for (size_t i = 0; i < functionReturn->numOperands(); ++i) {
            lower->operand(i) = functionReturn->operand(i).get();
            functionReturn->operand(i) = nullptr;
        }

        _bb->setBranch(std::move(lower));
    }else if (auto unconditional = hierarchy_cast<UnconditionalBranch *>(branch); unconditional) {
        auto lower = std::make_unique<JmpBranch>(unconditional->target);
        _bb->setBranch(std::move(lower));
    }else if (auto conditional = hierarchy_cast<ConditionalBranch *>(branch); conditional) {
        auto lower = std::make_unique<JnzBranch>(conditional->ifTarget, conditional->elseTarget);

        lower->operand = conditional->operand.get();
        conditional->operand = nullptr;

        _bb->setBranch(std::move(lower));
    } else {
        assert(!"Unexpected generic IR branch");
    }
}

std::unique_ptr<LowerCodePass> LowerCodePass::create(BasicBlock *bb) {
    return std::make_unique<LowerCodeImpl>(bb);
}

} // namespace lewis::targets::x86_64
