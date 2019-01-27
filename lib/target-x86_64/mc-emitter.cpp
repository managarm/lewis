// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>
#include <elf.h>
#include <lewis/target-x86_64/mc-emitter.hpp>

namespace lewis::targets::x86_64 {

MachineCodeEmitter::MachineCodeEmitter(Function *fn, elf::Object *elf)
: _fn{fn}, _elf{elf} { }

OperandSize getOperandSize(Value *v) {
    if (auto modeMValue = hierarchy_cast<ModeMValue *>(v); modeMValue) {
        return modeMValue->operandSize;
    } else {
        assert(!"Unexpected x86_64 IR value");
    }
}

int getRegister(Value *v) {
    if (auto modeMValue = hierarchy_cast<ModeMValue *>(v); modeMValue) {
        return modeMValue->modeRegister;
    } else {
        assert(!"Unexpected x86_64 IR value");
    }
}

void encodeRawRex(util::ByteEncoder &enc, OperandSize os, int r, int x, int b) {
    assert(r <= 1 && x <= 1 && b <= 1);
    int w = 0;
    if (os == OperandSize::qword)
        w = 1;
    if (w || r || x || b)
        encode8(enc, 0x40 | (w << 3) | (r << 2) | (x << 1) | b);
}

void encodeRawModRm(util::ByteEncoder &enc, int mod, int u, int x) {
    assert(mod <= 3 && x <= 7 && u <= 7);
    encode8(enc, (mod << 6) | (x << 3) | u);
}

void encodeRex(util::ByteEncoder &enc, Value *mv) {
    auto os = getOperandSize(mv);
    // TODO: Handle r8-r15 by setting REX.{X,B}.
    // REX.R is not used here.
    encodeRawRex(enc, os, 0, 0, 0);
}

void encodeModRm(util::ByteEncoder &enc, Value *mv, int extra) {
    auto mr = getRegister(mv);
    assert(mr >= 0);
    assert(extra >= 0 && extra <= 0x7);
    encodeRawModRm(enc, 3, mr, extra);
}

void encodeRex(util::ByteEncoder &enc, Value *mv, Value *rv) {
    auto os = getOperandSize(mv);
    // TODO: Handle instructions like movzx/movsx where this does not hold.
    assert(os == getOperandSize(rv));
    // TODO: Handle r8-r15 by setting REX.{R,X,B}.
    encodeRawRex(enc, os, 0, 0, 0);
}

void encodeModRm(util::ByteEncoder &enc, Value *mv, Value *rv) {
    auto mr = getRegister(mv);
    auto rr = getRegister(rv);
    assert(mr >= 0);
    assert(rr >= 0);
    encodeRawModRm(enc, 3, mr, rr);
}

void encodeModeWithDisp(util::ByteEncoder &enc, Value *mv, int32_t disp, Value *rv) {
    auto mr = getRegister(mv);
    auto rr = getRegister(rv);
    assert(mr >= 0);
    assert(rr >= 0);
    if (disp >= -128 && disp <= 127) {
        // Encode the displacement in 8 bits.
        encodeRawModRm(enc, 1, mr, rr);
        encode8(enc, disp);
    } else {
        // Encode the displacement in 32 bits.
        encodeRawModRm(enc, 2, mr, rr);
        encode32(enc, disp);
    }
}

void MachineCodeEmitter::run() {
    auto textString = _elf->addString(std::make_unique<lewis::elf::String>(".text"));
    auto symbolString = _elf->addString(std::make_unique<lewis::elf::String>(_fn->name));

    auto textSection = _elf->insertFragment(std::make_unique<lewis::elf::ByteSection>());
    textSection->name = textString;
    textSection->type = SHT_PROGBITS;
    textSection->flags = SHF_ALLOC | SHF_EXECINSTR;

    auto symbol = _elf->addSymbol(std::make_unique<lewis::elf::Symbol>());
    symbol->name = symbolString;
    symbol->section = textSection;

    for (auto bb : _fn->blocks())
        _emitBlock(bb, textSection);
}

void MachineCodeEmitter::_emitBlock(BasicBlock *bb, elf::ByteSection *textSection) {
    util::ByteEncoder text{&textSection->buffer};

    for (auto inst : bb->instructions()) {
        if (auto movMC = hierarchy_cast<MovMCInstruction *>(inst); movMC) {
            assert(getRegister(movMC->result.get()) >= 0);
            // TODO: Encode a REX prefix.
            encode8(text, 0xB8 + getRegister(movMC->result.get()));
            encode32(text, movMC->value);
        } else if (auto movMR = hierarchy_cast<MovMRInstruction *>(inst); movMR) {
            encodeRex(text, movMR->result.get(), movMR->operand.get());
            encode8(text, 0x89);
            encodeModRm(text, movMR->result.get(), movMR->operand.get());
        } else if (auto movRMWithOffset = hierarchy_cast<MovRMWithOffsetInstruction *>(inst);
                movRMWithOffset) {
            encodeRex(text, movRMWithOffset->operand.get(), movRMWithOffset->result.get());
            encode8(text, 0x8B);
            encodeModeWithDisp(text, movRMWithOffset->operand.get(), movRMWithOffset->offset,
                    movRMWithOffset->result.get());
        } else if (auto xchgMR = hierarchy_cast<XchgMRInstruction *>(inst); xchgMR) {
            encodeRex(text, xchgMR->firstResult.get(), xchgMR->secondResult.get());
            encode8(text, 0x87);
            encodeModRm(text, xchgMR->firstResult.get(), xchgMR->secondResult.get());
        } else if (auto negM = hierarchy_cast<NegMInstruction *>(inst); negM) {
            encodeRex(text, negM->result.get());
            encode8(text, 0xF7);
            encodeModRm(text, negM->result.get(), 3);
        } else if (auto addMR = hierarchy_cast<AddMRInstruction *>(inst); addMR) {
            encodeRex(text, addMR->result.get(), addMR->secondary.get());
            encode8(text, 0x01);
            encodeModRm(text, addMR->result.get(), addMR->secondary.get());
        } else if (auto andMR = hierarchy_cast<AndMRInstruction *>(inst); andMR) {
            encodeRex(text, andMR->result.get(), andMR->secondary.get());
            encode8(text, 0x21);
            encodeModRm(text, andMR->result.get(), andMR->secondary.get());
        }else if (auto call = hierarchy_cast<CallInstruction *>(inst); call) {
            auto string = _elf->addString(std::make_unique<elf::String>(call->function));
            auto symbol = _elf->addSymbol(std::make_unique<elf::Symbol>());
            symbol->name = string;

            auto relocation = _elf->addRelocation(std::make_unique<elf::Relocation>());
            relocation->section = textSection;
            relocation->offset = text.offset() + 1;
            relocation->symbol = symbol;

            encode8(text, 0xE8);
            encode32(text, 0); // Relocation points here.
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
