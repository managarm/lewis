
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
    size_t sectionIndex = 0;
    size_t offset = 64; // Size of EHDR.
    std::cout << "Running LayoutPass" << std::endl;
    for(auto fragment : _elf->fragments()) {
        size_t size;
        if(auto phdrs = hierarchy_cast<PhdrsFragment *>(fragment); phdrs) {
            // TODO: # of PHDRs should be independent of # of sections.
            size = _elf->numberOfFragments() * sizeof(Elf64_Phdr);
        }else if(auto shdrs = hierarchy_cast<ShdrsFragment *>(fragment); shdrs) {
            size = _elf->numberOfSections() * sizeof(Elf64_Shdr);
        }else if(auto strtab = hierarchy_cast<StringTableSection *>(fragment); strtab) {
            size = 1; // ELF uses index zero for non-existent strings.
            for(auto string : _elf->strings()) {
                string->designatedOffset = size;
                size += string->buffer.size() + 1;
            }
        }else{
            auto section = hierarchy_cast<ByteSection *>(fragment);
            assert(section && "Unexpected ELF fragment");
            size = section->buffer.size();
        }

        std::cout << "Laying out fragment " << fragment << " at " << (void *)offset
                << ", size: " << (void *)size << std::endl;
        if(fragment->isSection())
            fragment->designatedIndex = sectionIndex++;
        fragment->fileOffset = offset;
        fragment->computedSize = size;
        offset += size;
    }
}

std::unique_ptr<LayoutPass> LayoutPass::create(Object *elf) {
    return std::make_unique<LayoutPassImpl>(elf);
}

} // namespace lewis::elf

