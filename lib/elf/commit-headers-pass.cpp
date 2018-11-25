
#include <cassert>
#include <elf.h>
#include <lewis/elf/passes.hpp>
#include <lewis/elf/utils.hpp>

namespace lewis::elf {

struct CommitHeadersPassImpl : CommitHeadersPass {
    CommitHeadersPassImpl(Object *elf)
    : _elf{elf} { }

    void run() override;

private:
    void _inheritFragment(Fragment *out, Fragment *source);
    void _commitPhdrs(PhdrsReservation *phdrs);
    void _commitShdrs(ShdrsReservation *shdrs);
    void _commitStringTable(StringTableReservation *strtab);

    Object *_elf;
};

void CommitHeadersPassImpl::run() {
    for(auto fragment : _elf->fragments()) {
        if(auto phdrs = hierarchy_cast<PhdrsReservation *>(fragment); phdrs) {
            _commitPhdrs(phdrs);
        }else if(auto shdrs = hierarchy_cast<ShdrsReservation *>(fragment); shdrs) {
            _commitShdrs(shdrs);
        }else if(auto strtab = hierarchy_cast<StringTableReservation *>(fragment); strtab) {
            _commitStringTable(strtab);
        }
    }
}

void CommitHeadersPassImpl::_inheritFragment(Fragment *out, Fragment *source) {
    if(source->designatedIndex.has_value())
        out->designatedIndex = source->designatedIndex.value();
    if(source->fileOffset.has_value())
        out->fileOffset = source->fileOffset.value();
    if(source->computedSize.has_value())
        out->computedSize = source->computedSize.value();
}

void CommitHeadersPassImpl::_commitPhdrs(PhdrsReservation *phdrs) {
    auto out = std::make_unique<Section>();
    util::ByteEncoder section{&out->buffer};

    for(auto fragment : _elf->fragments()) {
        // TODO: p_type and p_flags are only fillers.
        encodeWord(section, PT_LOAD); // p_type
        encodeWord(section, PF_R | PF_X); // p_flags
        // TODO: p_offset and p_vaddr need not match.
        encodeOff(section, fragment->fileOffset.value()); // p_offset
        encodeAddr(section, fragment->fileOffset.value()); // p_vaddr
        encodeAddr(section, 0); // p_paddr
        // TODO: p_filesz and p_memsz need not match.
        encodeXword(section, fragment->computedSize.value()); // p_filesz
        encodeXword(section, fragment->computedSize.value()); // p_memsz
        encodeXword(section, 0); // p_align
    }

    _inheritFragment(out.get(), phdrs);
    _elf->replaceFragment(phdrs, std::move(out));
}

void CommitHeadersPassImpl::_commitShdrs(ShdrsReservation *shdrs) {
    auto out = std::make_unique<Section>();
    util::ByteEncoder section{&out->buffer};

    for(auto fragment : _elf->fragments()) {
        size_t nameIndex = 0;
        if(fragment->name) {
            assert(fragment->name->designatedOffset.has_value()
                    && "String table layout must be fixed for CommitHeadersPass");
            nameIndex = fragment->name->designatedOffset.value();
        }

        encodeWord(section, nameIndex); // sh_name
        // TODO: sh_type and sh_flags are only fillers.
        encodeWord(section, SHT_PROGBITS); // sh_type
        encodeXword(section, SHF_ALLOC | SHF_EXECINSTR); // sh_flags
        encodeAddr(section, 0); // sh_addr
        encodeOff(section, fragment->fileOffset.value()); // sh_offset
        encodeXword(section, fragment->computedSize.value()); // sh_size
        encodeWord(section, 0); // sh_link
        encodeWord(section, 0); // sh_info
        encodeXword(section, 0); // sh_addralign
        encodeXword(section, 0); // sh_entsize
    }

    _inheritFragment(out.get(), shdrs);
    _elf->replaceFragment(shdrs, std::move(out));
}

void CommitHeadersPassImpl::_commitStringTable(StringTableReservation *strtab) {
    auto out = std::make_unique<Section>();
    util::ByteEncoder section{&out->buffer};

    encode8(section, 0); // ELF uses index zero for non-existent strings.
    for(auto string : _elf->strings()) {
        encodeChars(section, string->buffer.c_str());
        encode8(section, 0);
    }

    _inheritFragment(out.get(), strtab);
    _elf->replaceFragment(strtab, std::move(out));
}

std::unique_ptr<CommitHeadersPass> CommitHeadersPass::create(Object *elf) {
    return std::make_unique<CommitHeadersPassImpl>(elf);
}

} // namespace lewis::elf

