
#include <cassert>
#include <elf.h>
#include <lewis/target-x86_64/mc-emitter.hpp>
#include <lewis/util/byte-encode.hpp>

namespace lewis::targets::x86_64 {

MachineCodeEmitter::MachineCodeEmitter(BasicBlock *bb, elf::Object *elf)
: _bb{bb}, _elf{elf} { }

void MachineCodeEmitter::run() {
    auto textString = std::make_unique<lewis::elf::String>(".text");
    auto newSection = std::make_unique<lewis::elf::ByteSection>();
    newSection->name = textString.get();
    newSection->type = SHT_PROGBITS;
    newSection->flags = SHF_ALLOC | SHF_EXECINSTR;

    auto textSection = newSection.get();
    _elf->insertFragment(std::move(newSection));
    _elf->addString(std::move(textString));

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

