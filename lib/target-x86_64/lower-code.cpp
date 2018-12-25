// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <cassert>
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
    for (auto inst : _bb->instructions()) {
        if (auto loadConst = hierarchy_cast<LoadConstInstruction *>(inst); loadConst) {
            auto lower = std::make_unique<MovMCInstruction>();
            lower->value = loadConst->value;
            loadConst->result()->replaceAllUses(lower->result());
            _bb->replaceInstruction(inst, std::move(lower));
        } else if (auto unaryMath = hierarchy_cast<UnaryMathInstruction *>(inst); unaryMath) {
            auto lower = std::make_unique<NegMInstruction>();
            lower->operand = unaryMath->operand.get();
            unaryMath->result()->replaceAllUses(lower->result());
            _bb->replaceInstruction(inst, std::move(lower));
        } else {
            assert(!"Unexpected generic IR instruction");
        }
    }
}

std::unique_ptr<LowerCodePass> LowerCodePass::create(BasicBlock *bb) {
    return std::make_unique<LowerCodeImpl>(bb);
}

} // namespace lewis::elf