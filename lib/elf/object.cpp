
#include <cassert>
#include <cstring>
#include <elf.h>
#include <lewis/elf/object.hpp>
#include <lewis/elf/utils.hpp>

namespace lewis::elf {

// --------------------------------------------------------------------------------------
// FragmentUse class
// --------------------------------------------------------------------------------------

void FragmentUse::assign(Fragment *f) {
    if(_ref) {
        auto it = _ref->_useList.iterator_to(this);
        _ref->_useList.erase(it);
    }

    if(f)
        f->_useList.push_back(this);
    _ref = f;
}

// --------------------------------------------------------------------------------------
// Fragment class
// --------------------------------------------------------------------------------------

void Fragment::replaceAllUses(Fragment *other) {
    auto it = _useList.begin();
    while(it != _useList.end()) {
        FragmentUse *use = *it;
        ++it;
        use->assign(other);
    }
}

// --------------------------------------------------------------------------------------
// Object class
// --------------------------------------------------------------------------------------

void Object::insertFragment(std::unique_ptr<Fragment> fragment) {
    _fragments.push_back(std::move(fragment));
}

void Object::replaceFragment(Fragment *from, std::unique_ptr<Fragment> to) {
    from->replaceAllUses(to.get());

    for(auto &slot : _fragments) {
        if(slot.get() != from)
            continue;
        slot = std::move(to);
        return;
    }

    assert(!"replaceFragment(): Fragment does not exist");
}

void Object::emitTo(FILE *stream) {
    auto emit = [&] (const std::vector<uint8_t> &buffer) {
        auto written = fwrite(buffer.data(), sizeof(uint8_t), buffer.size(), stream);
        if(written != buffer.size())
            throw std::runtime_error("Could not write buffer to FILE");
    };

    ByteVector ehdr;

    // Write the EHDR.e_ident field.
    encode8(ehdr, 0x7F);
    encodeChars(ehdr, "ELF");
    encode8(ehdr, ELFCLASS64);
    encode8(ehdr, ELFDATA2LSB);
    encode8(ehdr, 1); // ELF version; so far, there is only one.
    encode8(ehdr, ELFOSABI_SYSV);
    encode8(ehdr, 0); // ABI version. For the SysV ABI, this is not defined.
    for(int i = 0; i < 6; i++) // Encode a few padding bytes.
        encode8(ehdr, 0);
    encode8(ehdr, 0);

    // Write the remaining EHDR fields.
    assert(shdrsFragment);
    encodeHalf(ehdr, ET_DYN); // e_type
    encodeHalf(ehdr, EM_X86_64); // e_machine
    encodeWord(ehdr, 1); // e_version
    encodeAddr(ehdr, 0); // e_entry
    encodeOff(ehdr, phdrsFragment->fileOffset.value()); // e_phoff
    encodeOff(ehdr, shdrsFragment->fileOffset.value()); // e_shoff
    encodeWord(ehdr, 0); // e_flags
    // TODO: Do not hardcode this size.
    encodeHalf(ehdr, 64); // e_ehsize
    encodeHalf(ehdr, sizeof(Elf64_Phdr)); // e_phentsize
    // TODO: # of PHDRs should be independent of # of sections.
    encodeHalf(ehdr, fragments().size()); // e_phnum
    encodeHalf(ehdr, sizeof(Elf64_Shdr)); // e_shentsize
    encodeHalf(ehdr, fragments().size()); // e_shnum
    encodeHalf(ehdr, 0); // e_shstrndx

    emit(ehdr.buffer);

    for(auto it = _fragments.begin(); it != _fragments.end(); ++it) {
        auto section = dynamic_cast<Section *>(it->get());
        assert(section && "emitTo() can only handle Sections but not arbitrary Fragments");
        emit(section->buffer);
    }
}

} // namespace lewis::elf

