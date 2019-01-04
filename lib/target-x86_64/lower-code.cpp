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
    for (auto it = _bb->phis().begin(); it != _bb->phis().end(); ++it) {
        auto lower = std::make_unique<ModeMPhiNode>();
        for(auto edge : (*it)->edges())
            lower->attachEdge(std::make_unique<PhiEdge>(edge->source, edge->alias.get()));
        (*it)->replaceAllUses(lower.get());
        it = _bb->replacePhi(it, std::move(lower));
    }

    for (auto it = _bb->instructions().begin(); it != _bb->instructions().end(); ++it) {
        if (auto loadConst = hierarchy_cast<LoadConstInstruction *>(*it); loadConst) {
            auto lower = std::make_unique<MovMCInstruction>();
            lower->value = loadConst->value;
            loadConst->result()->replaceAllUses(lower->result());
            it = _bb->replaceInstruction(it, std::move(lower));
        } else if (auto unaryMath = hierarchy_cast<UnaryMathInstruction *>(*it); unaryMath) {
            std::unique_ptr<UnaryMInPlaceInstruction> lower;
            if (unaryMath->opcode == UnaryMathOpcode::negate) {
                lower = std::make_unique<NegMInstruction>();
            } else {
                assert(!"Unexpected unary math opcode");
            }
            lower->primary = unaryMath->operand.get();
            unaryMath->operand = nullptr;
            unaryMath->result()->replaceAllUses(lower->result());
            it = _bb->replaceInstruction(it, std::move(lower));
        } else if (auto binaryMath = hierarchy_cast<BinaryMathInstruction *>(*it); binaryMath) {
            std::unique_ptr<BinaryMRInPlaceInstruction> lower;
            if (binaryMath->opcode == BinaryMathOpcode::add) {
                lower = std::make_unique<AddMRInstruction>();
            }else if (binaryMath->opcode == BinaryMathOpcode::bitwiseAnd) {
                lower = std::make_unique<AndMRInstruction>();
            } else {
                assert(!"Unexpected binary math opcode");
            }
            lower->primary = binaryMath->left.get();
            lower->secondary = binaryMath->right.get();
            binaryMath->left = nullptr;
            binaryMath->right = nullptr;
            binaryMath->result()->replaceAllUses(lower->result());
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
