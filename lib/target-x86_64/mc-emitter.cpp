// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>
#include <elf.h>
#include <lewis/target-x86_64/mc-emitter.hpp>

namespace lewis::targets::x86_64 {

MachineCodeEmitter::MachineCodeEmitter(Function *fn, elf::Object *elf)
: _fn{fn}, _elf{elf} { }

int getRegister(Value *v) {
    if (auto phi = hierarchy_cast<PhiNode *>(v); phi) {
        auto modeM = hierarchy_cast<ModeMPhiNode *>(phi);
        assert(modeM);
        return modeM->modeRegister;
    }

    if (auto modeMResult = hierarchy_cast<ModeMResult *>(v); modeMResult) {
        return modeMResult->modeRegister;
    } else {
        assert(!"Unexpected x86_64 IR value");
    }
}

void encodeModRm(util::ByteEncoder &enc, int mod, int u, int x) {
    assert(mod <= 3 && x <= 7 && u <= 7);
    encode8(enc, (mod << 6) | (x << 3) | u);
}

void encodeMode(util::ByteEncoder &enc, Value *mv, int extra) {
    auto mr = getRegister(mv);
    assert(mr >= 0);
    assert(extra >= 0 && extra <= 0x7);
    encodeModRm(enc, 3, mr, extra);
}

void encodeMode(util::ByteEncoder &enc, Value *mv, Value *rv) {
    auto mr = getRegister(mv);
    auto rr = getRegister(rv);
    assert(mr >= 0);
    assert(rr >= 0);
    encodeModRm(enc, 3, mr, rr);
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
    for (auto bb : _fn->blocks())
        _emitBlock(bb, text);
}

void MachineCodeEmitter::_emitBlock(BasicBlock *bb, util::ByteEncoder &text) {
    for (auto inst : bb->instructions()) {
        if (auto movMC = hierarchy_cast<MovMCInstruction *>(inst); movMC) {
            assert(movMC->result()->modeRegister >= 0);
            encode8(text, 0xB8 + movMC->result()->modeRegister);
            encode32(text, movMC->value);
        } else if (auto movMR = hierarchy_cast<MovMRInstruction *>(inst); movMR) {
            encode8(text, 0x89);
            encodeMode(text, movMR->result(), movMR->operand.get());
        } else if (auto xchgMR = hierarchy_cast<XchgMRInstruction *>(inst); xchgMR) {
            encode8(text, 0x87);
            encodeMode(text, xchgMR->firstResult(), xchgMR->secondResult());
        } else if (auto negM = hierarchy_cast<NegMInstruction *>(inst); negM) {
            encode8(text, 0xF7);
            encodeMode(text, negM->result(), 3);
        } else {
            assert(!"Unexpected x86_64 IR instruction");
        }
    }

    auto branch = bb->branch();
    if (auto ret = hierarchy_cast<RetBranch *>(branch); ret) {
        encode8(text, 0xC3);
    }else if (auto jmp = hierarchy_cast<JmpBranch *>(branch); jmp) {
        encode8(text, 0xE9);
        encode32(text, 0); // TODO: Generate a relocation here.
    } else {
        assert(!"Unexpected x86_64 IR branch");
    }
}

} // namespace lewis::targets::x86_64
