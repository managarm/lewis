
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
    for(auto inst : _bb->instructions()) {
        auto loadConst = hierarchy_cast<LoadConstInstruction *>(inst);
        assert(loadConst && "Unexpected generic IR instruction");
        auto lower = std::make_unique<MovMCInstruction>();
        lower->value = loadConst->value;
        _bb->replaceInstruction(inst, std::move(lower));
    }
}

std::unique_ptr<LowerCodePass> LowerCodePass::create(BasicBlock *bb) {
    return std::make_unique<LowerCodeImpl>(bb);
}

} // namespace lewis::elf

