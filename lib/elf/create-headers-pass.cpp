
#include <lewis/elf/passes.hpp>
#include <lewis/elf/utils.hpp>

namespace lewis::elf {

struct CreateHeadersPassImpl : CreateHeadersPass {
    CreateHeadersPassImpl(Object *elf)
    : _elf{elf} { }

    void run() override;

private:
    Object *_elf;
};

void CreateHeadersPassImpl::run() {
    auto phdrs = std::make_unique<PhdrsReservation>();
    _elf->phdrsFragment = phdrs.get();
    _elf->insertFragment(std::move(phdrs));

    auto shdrs = std::make_unique<ShdrsReservation>();
    _elf->shdrsFragment = shdrs.get();
    _elf->insertFragment(std::move(shdrs));
}

std::unique_ptr<CreateHeadersPass> CreateHeadersPass::create(Object *elf) {
    return std::make_unique<CreateHeadersPassImpl>(elf);
}

} // namespace lewis::elf

