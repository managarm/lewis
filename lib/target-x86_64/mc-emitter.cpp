
#include <cassert>
#include <elf.h>
#include <lewis/target-x86_64/mc-emitter.hpp>
#include <lewis/util/byte-encode.hpp>

namespace lewis::targets::x86_64 {

MachineCodeEmitter::MachineCodeEmitter(BasicBlock *bb, elf::Object *elf)
: _bb{bb}, _elf{elf} { }

void MachineCodeEmitter::run() {
    auto textString = _elf->addString(std::make_unique<lewis::elf::String>(".text"));
    auto symbolString = _elf->addString(std::make_unique<lewis::elf::String>("doSomething"));

    auto textSection = _elf->insertFragment(std::make_unique<lewis::elf::ByteSection>());
    textSection->name = textString;
    textSection->type = SHT_PROGBITS;
    textSection->flags = SHF_ALLOC | SHF_EXECINSTR;

    auto symbol = _elf->addSymbol(std::make_unique<lewis::elf::Symbol>());
    symbol->name = symbolString;
    symbol->section = textSection;

    util::ByteEncoder text{&textSection->buffer};
    for(auto inst : _bb->instructions()) {
        if(auto code = hierarchy_cast<LoadConstCode *>(inst); code) {
            encode8(text, 0xB8);
            encode32(text, 0xDEADBEEF);
        }else{
            assert(!"Unexpected machine code instruction");
        }
    }
}


} // namespace lewis::targets::x86_64

