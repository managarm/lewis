
#include <cassert>
#include <iostream>
#include <elf.h>
#include <lewis/elf/passes.hpp>
#include <lewis/elf/utils.hpp>

namespace lewis::elf {

struct LayoutPassImpl : LayoutPass {
    LayoutPassImpl(Object *elf)
    : _elf{elf} { }

    void run() override;

private:
    Object *_elf;
};

void LayoutPassImpl::run() {
    size_t offset = 64; // Size of EHDR.
    std::cout << "Running LayoutPass" << std::endl;
    for(auto fragment : _elf->fragments()) {
        size_t size;
        if(auto phdrs = hierarchy_cast<PhdrsReservation *>(fragment); phdrs) {
            // TODO: # of PHDRs should be independent of # of sections.
            size = _elf->fragments().size() * sizeof(Elf64_Phdr);
        }else if(auto shdrs = hierarchy_cast<ShdrsReservation *>(fragment); shdrs) {
            size = _elf->fragments().size() * sizeof(Elf64_Shdr);
        }else{
            auto section = hierarchy_cast<Section *>(fragment);
            assert(section && "Unexpected ELF fragment");
            size = section->buffer.size();
        }

        std::cout << "Laying out fragment " << fragment << " at " << (void *)offset
                << ", size: " << (void *)size << std::endl;
        fragment->fileOffset = offset;
        fragment->computedSize = size;
        offset += size;
    }
}

std::unique_ptr<LayoutPass> LayoutPass::create(Object *elf) {
    return std::make_unique<LayoutPassImpl>(elf);
}

} // namespace lewis::elf

