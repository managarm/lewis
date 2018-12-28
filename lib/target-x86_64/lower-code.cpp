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
    for (auto it = _bb->instructions().begin(); it != _bb->instructions().end(); ++it) {
        if (auto loadConst = hierarchy_cast<LoadConstInstruction *>(*it); loadConst) {
            auto lower = std::make_unique<MovMCInstruction>();
            lower->value = loadConst->value;
            loadConst->result()->replaceAllUses(lower->result());
            it = _bb->replaceInstruction(it, std::move(lower));
        } else if (auto unaryMath = hierarchy_cast<UnaryMathInstruction *>(*it); unaryMath) {
            auto lower = std::make_unique<NegMInstruction>();
            lower->primary = unaryMath->operand.get();
            unaryMath->result()->replaceAllUses(lower->result());
            it = _bb->replaceInstruction(it, std::move(lower));
        } else {
            assert(!"Unexpected generic IR instruction");
        }
    }
}

std::unique_ptr<LowerCodePass> LowerCodePass::create(BasicBlock *bb) {
    return std::make_unique<LowerCodeImpl>(bb);
}

} // namespace lewis::targets::x86_64
