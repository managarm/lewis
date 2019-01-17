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
    auto lowerValue = [] (Value *) {
        return std::make_unique<ModeMValue>();
    };

    for (auto it = _bb->phis().begin(); it != _bb->phis().end(); ++it) {
        auto modeMValue = lowerValue((*it)->value.get());
        (*it)->value.get()->replaceAllUses(modeMValue.get());
        (*it)->value.set(std::move(modeMValue));
    }

    for (auto it = _bb->instructions().begin(); it != _bb->instructions().end(); ++it) {
        if (auto loadConst = hierarchy_cast<LoadConstInstruction *>(*it); loadConst) {
            auto lower = std::make_unique<MovMCInstruction>();
            auto lowerResult = lower->result.set(lowerValue(lower->result.get()));
            lower->value = loadConst->value;
            loadConst->result.get()->replaceAllUses(lowerResult);

            it = _bb->replaceInstruction(it, std::move(lower));
        } else if (auto loadOffset = hierarchy_cast<LoadOffsetInstruction *>(*it); loadOffset) {
            auto lower = std::make_unique<MovRMWithOffsetInstruction>(loadOffset->operand.get(),
                    loadOffset->offset);
            auto lowerResult = lower->result.set(lowerValue(lower->result.get()));
            loadOffset->result.get()->replaceAllUses(lowerResult);

            loadOffset->operand = nullptr;
            it = _bb->replaceInstruction(it, std::move(lower));
        } else if (auto unaryMath = hierarchy_cast<UnaryMathInstruction *>(*it); unaryMath) {
            std::unique_ptr<UnaryMInPlaceInstruction> lower;
            if (unaryMath->opcode == UnaryMathOpcode::negate) {
                lower = std::make_unique<NegMInstruction>();
            } else {
                assert(!"Unexpected unary math opcode");
            }
            auto lowerResult = lower->result.set(lowerValue(lower->result.get()));
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
            auto lowerResult = lower->result.set(lowerValue(lower->result.get()));
            lower->primary = binaryMath->left.get();
            lower->secondary = binaryMath->right.get();
            binaryMath->result.get()->replaceAllUses(lowerResult);

            binaryMath->left = nullptr;
            binaryMath->right = nullptr;
            it = _bb->replaceInstruction(it, std::move(lower));
        } else if (auto invoke = hierarchy_cast<InvokeInstruction *>(*it); invoke) {
            auto lower = std::make_unique<CallInstruction>();
            auto lowerResult = lower->result.set(lowerValue(lower->result.get()));
            lower->function = invoke->function;
            lower->operand = invoke->operand.get();
            invoke->result.get()->replaceAllUses(lowerResult);

            invoke->operand = nullptr;
            it = _bb->replaceInstruction(it, std::move(lower));
        } else {
            assert(!"Unexpected generic IR instruction");
        }
    }

    auto branch = _bb->branch();
    if (auto functionReturn = hierarchy_cast<FunctionReturnBranch *>(branch); functionReturn) {
        auto lower = std::make_unique<RetBranch>();
        _bb->setBranch(std::move(lower));
    }else if (auto unconditional = hierarchy_cast<UnconditionalBranch *>(branch); unconditional) {
        auto lower = std::make_unique<JmpBranch>(unconditional->target);
        _bb->setBranch(std::move(lower));
    } else {
        assert(!"Unexpected generic IR branch");
    }
}

std::unique_ptr<LowerCodePass> LowerCodePass::create(BasicBlock *bb) {
    return std::make_unique<LowerCodeImpl>(bb);
}

} // namespace lewis::targets::x86_64
