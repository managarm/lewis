
#include <elf.h>
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

    auto strtab = std::make_unique<StringTableReservation>();
    strtab->type = SHT_STRTAB;
    strtab->flags = SHF_ALLOC;
    _elf->stringTableFragment = strtab.get();
    _elf->insertFragment(std::move(strtab));
}

std::unique_ptr<CreateHeadersPass> CreateHeadersPass::create(Object *elf) {
    return std::make_unique<CreateHeadersPassImpl>(elf);
}

} // namespace lewis::elf

