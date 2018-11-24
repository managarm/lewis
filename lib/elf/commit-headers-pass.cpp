
#include <elf.h>
#include <lewis/elf/passes.hpp>
#include <lewis/elf/utils.hpp>

namespace lewis::elf {

struct CommitHeadersPassImpl : CommitHeadersPass {
    CommitHeadersPassImpl(Object *elf)
    : _elf{elf} { }

    void run() override;

private:
    void _commitPhdrs(PhdrsReservation *phdrs);
    void _commitShdrs(ShdrsReservation *shdrs);

    Object *_elf;
};

void CommitHeadersPassImpl::run() {
    for(auto fragment : _elf->fragments()) {
        if(auto phdrs = hierarchy_cast<PhdrsReservation *>(fragment); phdrs) {
            _commitPhdrs(phdrs);
        }else if(auto shdrs = hierarchy_cast<ShdrsReservation *>(fragment); shdrs) {
            _commitShdrs(shdrs);
        }
    }
}

void CommitHeadersPassImpl::_commitPhdrs(PhdrsReservation *phdrs) {
    auto out = std::make_unique<Section>();
    if(phdrs->fileOffset.has_value())
        out->fileOffset = phdrs->fileOffset.value();
    if(phdrs->computedSize.has_value())
        out->computedSize = phdrs->computedSize.value();

    for(auto section : _elf->fragments()) {
        ByteEncoder shdr{&out->buffer};
        // TODO: p_type and p_flags are only fillers.
        encodeWord(shdr, PT_LOAD); // p_type
        encodeWord(shdr, PF_R | PF_X); // p_flags
        // TODO: p_offset and p_vaddr need not match.
        encodeOff(shdr, section->fileOffset.value()); // p_offset
        encodeAddr(shdr, section->fileOffset.value()); // p_vaddr
        encodeAddr(shdr, 0); // p_paddr
        // TODO: p_filesz and p_memsz need not match.
        encodeXword(shdr, section->computedSize.value()); // p_filesz
        encodeXword(shdr, section->computedSize.value()); // p_memsz
        encodeXword(shdr, 0); // p_align
    }

    _elf->replaceFragment(phdrs, std::move(out));
}

void CommitHeadersPassImpl::_commitShdrs(ShdrsReservation *shdrs) {
    auto out = std::make_unique<Section>();
    if(shdrs->fileOffset.has_value())
        out->fileOffset = shdrs->fileOffset.value();
    if(shdrs->fileOffset.has_value())
        out->fileOffset = shdrs->fileOffset.value();

    for(auto section : _elf->fragments()) {
        ByteEncoder shdr{&out->buffer};
        // TODO: Emit section names.
        encodeWord(shdr, 0); // sh_name
        // TODO: sh_type and sh_flags are only fillers.
        encodeWord(shdr, SHT_PROGBITS); // sh_type
        encodeXword(shdr, SHF_ALLOC | SHF_EXECINSTR); // sh_flags
        encodeAddr(shdr, 0); // sh_addr
        encodeOff(shdr, section->fileOffset.value()); // sh_offset
        encodeXword(shdr, section->computedSize.value()); // sh_size
        encodeWord(shdr, 0); // sh_link
        encodeWord(shdr, 0); // sh_info
        encodeXword(shdr, 0); // sh_addralign
        encodeXword(shdr, 0); // sh_entsize
    }

    _elf->replaceFragment(shdrs, std::move(out));
}

std::unique_ptr<CommitHeadersPass> CommitHeadersPass::create(Object *elf) {
    return std::make_unique<CommitHeadersPassImpl>(elf);
}

} // namespace lewis::elf

