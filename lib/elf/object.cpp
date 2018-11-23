
#include <cstring>
#include <elf.h>
#include <lewis/elf/object.hpp>

struct ByteEncoder {
    ByteEncoder(std::vector<uint8_t> *out)
    : _out{out} { }

private:
    template<typename T>
    void _poke(T v) {
        auto n = _out->size();
        _out->resize(n + sizeof(T));
        memcpy(_out->data() + n, &v, sizeof(T));
    }

public:
    friend void encodeChars(ByteEncoder &e, const char *v) {
        while(*v)
            e._poke<uint8_t>(*(v++));
    }
    friend void encode8(ByteEncoder &e, uint8_t v) { e._poke<uint8_t>(v); }
    friend void encode16(ByteEncoder &e, uint16_t v) { e._poke<uint16_t>(v); }
    friend void encode32(ByteEncoder &e, uint32_t v) { e._poke<uint32_t>(v); }
    friend void encode64(ByteEncoder &e, uint64_t v) { e._poke<uint64_t>(v); }

private:
    std::vector<uint8_t> *_out;
};

void encodeAddr(ByteEncoder &e, uint64_t v) { encode64(e, v); }
void encodeOff(ByteEncoder &e, uint64_t v) { encode64(e, v); }
void encodeHalf(ByteEncoder &e, uint16_t v) { encode16(e, v); }
void encodeWord(ByteEncoder &e, uint32_t v) { encode32(e, v); }

struct ByteVector : ByteEncoder {
    ByteVector()
    : ByteEncoder{&buffer} { }

    std::vector<uint8_t> buffer;
};

namespace lewis::elf {

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
    encodeHalf(ehdr, ET_DYN); // e_type
    encodeHalf(ehdr, EM_X86_64); // e_machine
    encodeWord(ehdr, 1); // e_version
    encodeAddr(ehdr, 0); // e_entry
    encodeOff(ehdr, 0); // e_phoff
    encodeOff(ehdr, 0); // e_shoff
    encodeWord(ehdr, 0); // e_flags
    encodeHalf(ehdr, 64); // e_ehsize
    encodeHalf(ehdr, 0); // e_phentsize
    encodeHalf(ehdr, 0); // e_phnum
    encodeHalf(ehdr, 0); // e_shentsize
    encodeHalf(ehdr, 0); // e_shnum
    encodeHalf(ehdr, 0); // e_shstrndx

    emit(ehdr.buffer);
}

} // namespace lewis::elf

