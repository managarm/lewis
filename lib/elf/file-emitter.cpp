// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>
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
    void _emitDynamic(DynamicSection *dynamic);
    void _emitStringTable(StringTableSection *strtab);
    void _emitSymbolTable(SymbolTableSection *symtab);
    void _emitRela(RelocationSection *rel);
    void _emitHash(HashSection *hash);

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
    for (int i = 0; i < 7; i++) // The remaining e_ident bytes are padding.
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
    encodeHalf(ehdr, _elf->numberOfFragments() + 1); // e_phnum
    encodeHalf(ehdr, sizeof(Elf64_Shdr)); // e_shentsize
    encodeHalf(ehdr, 1 + _elf->numberOfSections()); // e_shnum
    encodeHalf(ehdr, _elf->stringTableFragment->designatedIndex.value()); // e_shstrndx

    for (auto fragment : _elf->fragments()) {
        // Make sure that sections are at least 8-byte aligned.
        // TODO: Support arbitrary alignment.
        while(buffer.size() & size_t(7))
            buffer.push_back(0);

        // Make sure that the fragment ends up at the correct address.
        assert(fragment->fileOffset.value() == buffer.size());

        if (auto phdrs = hierarchy_cast<PhdrsFragment *>(fragment); phdrs) {
            _emitPhdrs(phdrs);
        } else if (auto shdrs = hierarchy_cast<ShdrsFragment *>(fragment); shdrs) {
            _emitShdrs(shdrs);
        } else if (auto dynamic = hierarchy_cast<DynamicSection *>(fragment); dynamic) {
            _emitDynamic(dynamic);
        } else if (auto strtab = hierarchy_cast<StringTableSection *>(fragment); strtab) {
            _emitStringTable(strtab);
        } else if (auto symtab = hierarchy_cast<SymbolTableSection *>(fragment); symtab) {
            _emitSymbolTable(symtab);
        } else if (auto rel = hierarchy_cast<RelocationSection *>(fragment); rel) {
            _emitRela(rel);
        } else if (auto hash = hierarchy_cast<HashSection *>(fragment); hash) {
            _emitHash(hash);
        } else {
            auto section = hierarchy_cast<ByteSection *>(fragment);
            assert(section && "Unexpected Fragment for FileEmitter");
            buffer.insert(buffer.end(), section->buffer.begin(), section->buffer.end());
        }
    }
}

void FileEmitterImpl::_emitPhdrs(PhdrsFragment *phdrs) {
    util::ByteEncoder section{&buffer};

    for (auto fragment : _elf->fragments()) {
        // TODO: p_type and p_flags are only fillers.
        encodeWord(section, PT_LOAD); // p_type
        encodeWord(section, PF_R | PF_X); // p_flags
        encodeOff(section, fragment->fileOffset.value()); // p_offset
        encodeAddr(section, fragment->virtualAddress.value()); // p_vaddr
        encodeAddr(section, fragment->virtualAddress.value()); // p_paddr
        // TODO: p_filesz and p_memsz need not match.
        encodeXword(section, fragment->computedSize.value()); // p_filesz
        encodeXword(section, fragment->computedSize.value()); // p_memsz
        encodeXword(section, 0); // p_align
    }

    // Emit the PT_DYNAMIC segment.
    encodeWord(section, PT_DYNAMIC); // p_type
    encodeWord(section, PF_R); // p_flags
    encodeOff(section, _elf->dynamicFragment->fileOffset.value()); // p_offset
    encodeAddr(section, _elf->dynamicFragment->virtualAddress.value()); // p_vaddr
    encodeAddr(section, _elf->dynamicFragment->virtualAddress.value()); // p_paddr
    encodeXword(section, _elf->dynamicFragment->computedSize.value()); // p_filesz
    encodeXword(section, _elf->dynamicFragment->computedSize.value()); // p_memsz
    encodeXword(section, 0); // p_align
}

void FileEmitterImpl::_emitShdrs(ShdrsFragment *shdrs) {
    util::ByteEncoder section{&buffer};

    // Emit the SHN_UNDEF section. Specified in the ELF base specification.
    encodeWord(section, 0); // sh_name
    encodeWord(section, SHT_NULL); // sh_type
    encodeXword(section, 0); // sh_flags
    encodeAddr(section, 0); // sh_addr
    encodeOff(section, 0); // sh_offset
    encodeXword(section, 0); // sh_size
    encodeWord(section, SHN_UNDEF); // sh_link
    encodeWord(section, 0); // sh_info
    encodeXword(section, 0); // sh_addralign
    encodeXword(section, 0); // sh_entsize

    // Emit all "real sections.
    for (auto fragment : _elf->fragments()) {
        if (!fragment->isSection())
            continue;

        size_t nameIndex = 0;
        if (fragment->name) {
            assert(fragment->name->designatedOffset.has_value()
                    && "String table layout must be fixed for FileEmitter");
            nameIndex = fragment->name->designatedOffset.value();
        }

        size_t linkIndex = 0;
        if (fragment->sectionLink) {
            assert(fragment->sectionLink->designatedIndex.has_value()
                    && "Section layout must be fixed for FileEmitter");
            linkIndex = fragment->sectionLink->designatedIndex.value();
        }

        encodeWord(section, nameIndex); // sh_name
        encodeWord(section, fragment->type); // sh_type
        encodeXword(section, fragment->flags); // sh_flags
        encodeAddr(section, fragment->virtualAddress.value()); // sh_addr
        encodeOff(section, fragment->fileOffset.value()); // sh_offset
        encodeXword(section, fragment->computedSize.value()); // sh_size
        encodeWord(section, linkIndex); // sh_link
        encodeWord(section, fragment->sectionInfo.value_or(0)); // sh_info
        encodeXword(section, 0); // sh_addralign
        encodeXword(section, fragment->entrySize.value_or(0)); // sh_entsize
    }
}

void FileEmitterImpl::_emitDynamic(DynamicSection *dynamic) {
    util::ByteEncoder section{&buffer};

    encodeSxword(section, DT_STRTAB);
    encodeXword(section, _elf->stringTableFragment->virtualAddress.value());
    encodeSxword(section, DT_SYMTAB);
    encodeXword(section, _elf->symbolTableFragment->virtualAddress.value());
    encodeSxword(section, DT_HASH);
    encodeXword(section, _elf->hashFragment->virtualAddress.value());
    encodeSxword(section, DT_JMPREL);
    encodeXword(section, _elf->pltRelocationFragment->virtualAddress.value());
    encodeSxword(section, DT_PLTRELSZ);
    encodeXword(section, _elf->pltRelocationFragment->computedSize.value());
    encodeSxword(section, DT_NULL);
    encodeXword(section, 0);
}

void FileEmitterImpl::_emitStringTable(StringTableSection *strtab) {
    util::ByteEncoder section{&buffer};

    encode8(section, 0); // ELF uses index zero for non-existent strings.
    for (auto string : _elf->strings()) {
        encodeChars(section, string->buffer.c_str());
        encode8(section, 0);
    }
}

void FileEmitterImpl::_emitSymbolTable(SymbolTableSection *symtab) {
    util::ByteEncoder section{&buffer};

    // Encode the null symbol.
    encodeWord(section, 0); // st_name
    encode8(section, 0); // st_info
    encode8(section, 0); // st_other
    encodeHalf(section, 0); // st_shndx
    encodeAddr(section, 0); // st_value
    encodeXword(section, 0); // st_size

    // Encode all "real" symbols.
    for (auto symbol : _elf->symbols()) {
        size_t nameIndex = 0;
        if (symbol->name) {
            assert(symbol->name->designatedOffset.has_value()
                    && "String table layout must be fixed for FileEmitter");
            nameIndex = symbol->name->designatedOffset.value();
        }

        size_t sectionIndex = 0;
        uint64_t virtualAddress = 0;
        if (symbol->section) {
            assert(symbol->section->designatedIndex.has_value()
                    && "Section layout must be fixed for FileEmitter");
            assert(symbol->section->virtualAddress.has_value()
                    && "Section layout must be fixed for FileEmitter");
            sectionIndex = symbol->section->designatedIndex.value();
            virtualAddress = symbol->section->virtualAddress.value() + symbol->value;
        }

        encodeWord(section, nameIndex); // st_name
        encode8(section, ELF64_ST_INFO(STB_GLOBAL, STT_FUNC)); // st_info
        encode8(section, 0); // st_other
        encodeHalf(section, sectionIndex); // st_shndx
        // TODO: Use symbol->value in object files.
        encodeAddr(section, virtualAddress); // st_value
        encodeXword(section, 0); // st_size
    }
}

void FileEmitterImpl::_emitRela(RelocationSection *rel) {
    util::ByteEncoder section{&buffer};

    for (auto relocation : _elf->relocations()) {
        assert(relocation->offset >= 0);

        assert(relocation->section && "Section layout must be fixed for FileEmitter");
        assert(relocation->section->virtualAddress.has_value()
                && "Section layout must be fixed for FileEmitter");
        size_t sectionAddress = relocation->section->virtualAddress.value();

        size_t symbolIndex = 0;
        if (relocation->symbol) {
            assert(relocation->symbol->designatedIndex.has_value()
                    && "Symbol table layout must be fixed for FileEmitter");
            symbolIndex = relocation->symbol->designatedIndex.value();
        }

        encodeAddr(section, sectionAddress + relocation->offset);
        encodeXword(section, (symbolIndex << 32) | R_X86_64_JUMP_SLOT);
        encodeSxword(section, 0);
    }
}

void FileEmitterImpl::_emitHash(HashSection *hash) {
    util::ByteEncoder section{&buffer};

    encodeWord(section, hash->buckets.size());
    encodeWord(section, hash->chains.size());

    for (auto symbol : hash->buckets) {
        if (symbol) {
            encodeWord(section, symbol->designatedIndex.value());
        }else{
            encodeWord(section, 0);
        }
    }

    for (auto symbol : hash->chains) {
        if (symbol) {
            encodeWord(section, symbol->designatedIndex.value());
        }else{
            encodeWord(section, 0);
        }
    }
}

std::unique_ptr<FileEmitter> FileEmitter::create(Object *elf) {
    return std::make_unique<FileEmitterImpl>(elf);
}

} // namespace lewis::elf
