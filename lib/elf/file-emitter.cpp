
#include <cassert>
#include <elf.h>
#include <lewis/elf/file-emitter.hpp>
#include <lewis/elf/passes.hpp>
#include <lewis/elf/utils.hpp>

namespace lewis::elf {

struct FileEmitterImpl : FileEmitter {
    FileEmitterImpl(Object *elf)
    : _elf{elf} { }

    void run() override;

private:
    void _emitPhdrs(PhdrsFragment *phdrs);
    void _emitShdrs(ShdrsFragment *shdrs);
    void _emitStringTable(StringTableSection *strtab);

    Object *_elf;
};

void FileEmitterImpl::run() {
    util::ByteEncoder ehdr{&buffer};

    // Write the EHDR.e_ident field.
    encode8(ehdr, 0x7F);
    encodeChars(ehdr, "ELF");
    encode8(ehdr, ELFCLASS64);
    encode8(ehdr, ELFDATA2LSB);
    encode8(ehdr, 1); // ELF version; so far, there is only one.
    encode8(ehdr, ELFOSABI_SYSV);
    encode8(ehdr, 0); // ABI version. For the SysV ABI, this is not defined.
    for(int i = 0; i < 7; i++) // The remaining e_ident bytes are padding.
        encode8(ehdr, 0);

    // Write the remaining EHDR fields.
    assert(_elf->phdrsFragment);
    assert(_elf->shdrsFragment);
    assert(_elf->stringTableFragment);
    encodeHalf(ehdr, ET_DYN); // e_type
    encodeHalf(ehdr, EM_X86_64); // e_machine
    encodeWord(ehdr, 1); // e_version
    encodeAddr(ehdr, 0); // e_entry
    encodeOff(ehdr, _elf->phdrsFragment->fileOffset.value()); // e_phoff
    encodeOff(ehdr, _elf->shdrsFragment->fileOffset.value()); // e_shoff
    encodeWord(ehdr, 0); // e_flags
    // TODO: Do not hardcode this size.
    encodeHalf(ehdr, 64); // e_ehsize
    encodeHalf(ehdr, sizeof(Elf64_Phdr)); // e_phentsize
    // TODO: # of PHDRs should be independent of # of sections.
    encodeHalf(ehdr, _elf->numberOfFragments()); // e_phnum
    encodeHalf(ehdr, sizeof(Elf64_Shdr)); // e_shentsize
    encodeHalf(ehdr, _elf->numberOfSections()); // e_shnum
    encodeHalf(ehdr, _elf->stringTableFragment->designatedIndex.value()); // e_shstrndx

    for(auto fragment : _elf->fragments()) {
        if(auto phdrs = hierarchy_cast<PhdrsFragment *>(fragment); phdrs) {
            _emitPhdrs(phdrs);
        }else if(auto shdrs = hierarchy_cast<ShdrsFragment *>(fragment); shdrs) {
            _emitShdrs(shdrs);
        }else if(auto strtab = hierarchy_cast<StringTableSection *>(fragment); strtab) {
            _emitStringTable(strtab);
        }else{
            auto section = hierarchy_cast<ByteSection *>(fragment);
            assert(section && "Unexpected Fragment for FileEmitter");
            buffer.insert(buffer.end(), section->buffer.begin(), section->buffer.end());
        }
    }
}

void FileEmitterImpl::_emitPhdrs(PhdrsFragment *phdrs) {
    util::ByteEncoder section{&buffer};

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
}

void FileEmitterImpl::_emitShdrs(ShdrsFragment *shdrs) {
    util::ByteEncoder section{&buffer};

    for(auto fragment : _elf->fragments()) {
        if(!fragment->isSection())
            continue;

        size_t nameIndex = 0;
        if(fragment->name) {
            assert(fragment->name->designatedOffset.has_value()
                    && "String table layout must be fixed for FileEmitter");
            nameIndex = fragment->name->designatedOffset.value();
        }

        encodeWord(section, nameIndex); // sh_name
        // TODO: sh_type and sh_flags are only fillers.
        encodeWord(section, fragment->type); // sh_type
        encodeXword(section, fragment->flags); // sh_flags
        encodeAddr(section, fragment->fileOffset.value()); // sh_addr
        encodeOff(section, fragment->fileOffset.value()); // sh_offset
        encodeXword(section, fragment->computedSize.value()); // sh_size
        encodeWord(section, 0); // sh_link
        encodeWord(section, 0); // sh_info
        encodeXword(section, 0); // sh_addralign
        encodeXword(section, 0); // sh_entsize
    }
}

void FileEmitterImpl::_emitStringTable(StringTableSection *strtab) {
    util::ByteEncoder section{&buffer};

    encode8(section, 0); // ELF uses index zero for non-existent strings.
    for(auto string : _elf->strings()) {
        encodeChars(section, string->buffer.c_str());
        encode8(section, 0);
    }
}

std::unique_ptr<FileEmitter> FileEmitter::create(Object *elf) {
    return std::make_unique<FileEmitterImpl>(elf);
}

} // namespace lewis::elf

