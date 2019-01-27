// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <cassert>
#include <cstring>
#include <iostream>
#include <elf.h>
#include <lewis/elf/passes.hpp>

namespace lewis::elf {

namespace {
    void put32(uint8_t *p, uint32_t v) {
        memcpy(p, &v, sizeof(uint32_t));
    }
};

struct InternalLinkPassImpl : InternalLinkPass {
    InternalLinkPassImpl(Object *elf)
    : _elf{elf} { }

    void run() override;

private:
    Object *_elf;
};

void InternalLinkPassImpl::run() {
    std::cout << "Running InternalLinkPass" << std::endl;
    for (auto relocation : _elf->internalRelocations()) {
        assert(relocation->offset >= 0);

        assert(relocation->section);
        assert(relocation->section->virtualAddress.has_value()
                && "Section layout must be fixed for InternalLinkPass");
        auto relocationAddress = relocation->section->virtualAddress.value() + relocation->offset;

        auto symbol = relocation->symbol;
        assert(symbol->section);
        assert(symbol->section->virtualAddress.has_value()
                && "Section layout must be fixed for InternalLinkPass");
        auto symbolAddress = symbol->section->virtualAddress.value() + symbol->value;

        // Here, we emit a R_X86_64_PC32 relocation. TODO: Support other types of relocations.
        auto byteSection = hierarchy_cast<ByteSection *>(relocation->section.get());
        auto value = symbolAddress - relocationAddress + relocation->addend.value_or(0);
        put32(byteSection->buffer.data() + relocation->offset, value);
    }
}

std::unique_ptr<InternalLinkPass> InternalLinkPass::create(Object *elf) {
    return std::make_unique<InternalLinkPassImpl>(elf);
}

} // namespace lewis::elf
