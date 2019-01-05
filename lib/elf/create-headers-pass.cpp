// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

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
    auto phdrs = _elf->insertFragment(std::make_unique<PhdrsFragment>());
    _elf->phdrsFragment = phdrs;

    auto shdrs = _elf->insertFragment(std::make_unique<ShdrsFragment>());
    _elf->shdrsFragment = shdrs;

    auto strtab = _elf->insertFragment(std::make_unique<StringTableSection>());
    strtab->type = SHT_STRTAB;
    strtab->flags = SHF_ALLOC;
    _elf->stringTableFragment = strtab;

    auto symtab = _elf->insertFragment(std::make_unique<SymbolTableSection>());
    symtab->type = SHT_SYMTAB;
    symtab->flags = SHF_ALLOC;
    symtab->sectionLink = strtab;
    symtab->sectionInfo = 1;
    symtab->entrySize = sizeof(Elf64_Sym);
    _elf->symbolTableFragment = symtab;

    auto pltrel = _elf->insertFragment(std::make_unique<RelocationSection>());
    pltrel->type = SHT_RELA;
    pltrel->flags = SHF_ALLOC;
    pltrel->sectionLink = symtab;
    //pltrel->sectionInfo = 1; // TODO: Index of the section the relocations apply to.
    pltrel->entrySize = sizeof(Elf64_Rela);
}

std::unique_ptr<CreateHeadersPass> CreateHeadersPass::create(Object *elf) {
    return std::make_unique<CreateHeadersPassImpl>(elf);
}

} // namespace lewis::elf
