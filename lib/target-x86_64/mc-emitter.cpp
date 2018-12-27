// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <cassert>
#include <elf.h>
#include <lewis/target-x86_64/mc-emitter.hpp>
#include <lewis/util/byte-encode.hpp>

namespace lewis::targets::x86_64 {

MachineCodeEmitter::MachineCodeEmitter(BasicBlock *bb, elf::Object *elf)
: _bb{bb}, _elf{elf} { }

void encodeModRm(util::ByteEncoder &enc, int mod, int u, int x) {
    assert(mod <= 3 && x <= 7 && u <= 7);
    encode8(enc, (mod << 6) | (x << 3) | u);
}

void encodeMode(util::ByteEncoder &enc, ModeMResult *v, int extra) {
    assert(v->modeRegister >= 0);
    assert(extra <= 0x7);
    encodeModRm(enc, 3, v->modeRegister, extra);
}

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
    for (auto inst : _bb->instructions()) {
        if (auto movMC = hierarchy_cast<MovMCInstruction *>(inst); movMC) {
            assert(movMC->result()->modeRegister >= 0);
            encode8(text, 0xB8 + movMC->result()->modeRegister);
            encode32(text, movMC->value);
        } else if (auto negM = hierarchy_cast<NegMInstruction *>(inst); negM) {
            encode8(text, 0xF7);
            encodeMode(text, negM->result(), 3);
        } else {
            assert(!"Unexpected machine code instruction");
        }
    }
}

} // namespace lewis::targets::x86_64
