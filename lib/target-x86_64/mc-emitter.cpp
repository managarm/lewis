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
    if (auto registerMode = hierarchy_cast<RegisterMode *>(v); registerMode) {
        return registerMode->operandSize;
    } else if (auto baseDisp = hierarchy_cast<BaseDispMemoryMode *>(v); baseDisp) {
        return baseDisp->operandSize;
    } else {
        assert(!"Unexpected x86_64 IR value");
    }
}

int getRegister(Value *v) {
    if (auto registerMode = hierarchy_cast<RegisterMode *>(v); registerMode) {
        return registerMode->modeRegister;
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

// mod: Value of the 'mod' field.
// m: Value of the 'M' field.
// x: Value of the 'R' field (also used as extra bit for the opcode).
void encodeRawModRm(util::ByteEncoder &enc, int mod, int m, int x) {
    assert(mod <= 3 && x <= 7 && m <= 7);
    encode8(enc, (mod << 6) | (x << 3) | m);
}

// b: Value of the 'base' field.
// i: Value of the 'index' field.
// s: Value of the 'SS' field.
void encodeRawSib(util::ByteEncoder &enc, int b, int i, int s) {
    assert(s <= 3 && i <= 7 && b <= 7);
    encode8(enc, (s << 6) | (i << 3) | b);
}

struct ModRmEncoding {
    ModRmEncoding(Value *mv, Value *rv)
    : _mv{mv}, _rv{rv}, _xop{-1} { }

    ModRmEncoding(Value *mv, int xop)
    : _mv{mv}, _rv{nullptr}, _xop{xop} {
        assert(xop <= 7);
    }

    void encodeRex(util::ByteEncoder &enc) {
        auto os = getOperandSize(_mv);
        if (_rv)
            assert(os == getOperandSize(_rv));

        int b;
        if (auto registerMode = hierarchy_cast<RegisterMode *>(_mv); registerMode) {
            assert(registerMode->modeRegister >= 0);
            b = registerMode->modeRegister >= 8;
        } else if (auto baseDisp = hierarchy_cast<BaseDispMemoryMode *>(_mv); baseDisp) {
            assert(baseDisp->baseRegister >= 0);
            b = baseDisp->baseRegister >= 8;
        } else {
            assert(!"Unexpected x86_64 IR value");
            abort();
        }

        encodeRawRex(enc, os, _x() >= 8, 0, b);
    }

    void encodeModRmSib(util::ByteEncoder &enc) {
        if (auto registerMode = hierarchy_cast<RegisterMode *>(_mv); registerMode) {
            assert(registerMode->modeRegister >= 0);
            encodeRawModRm(enc, 3, registerMode->modeRegister & 7, _x() & 7);
        } else if (auto baseDisp = hierarchy_cast<BaseDispMemoryMode *>(_mv); baseDisp) {
            assert(baseDisp->baseRegister >= 0);
            assert((baseDisp->baseRegister & 7) != 5
                    && "RSP/R12 need an SIB-byte to encode BaseDispMemoryMode");
            if (baseDisp->disp >= -128 && baseDisp->disp <= 127) {
                // Encode the displacement in 8 bits.
                encodeRawModRm(enc, 1, baseDisp->baseRegister & 7, _x() & 7);
                encode8(enc, baseDisp->disp);
            } else {
                // Encode the displacement in 32 bits.
                encodeRawModRm(enc, 2, baseDisp->baseRegister & 7, _x() & 7);
                encode32(enc, baseDisp->disp);
            }
        } else {
            assert(!"Unexpected x86_64 IR value");
            abort();
        }

    }

private:
    int _x() {
        if(_rv) {
            auto rr = getRegister(_rv);
            assert(rr >= 0);
            return rr;
        }else{
            assert(_xop >= 0);
            return _xop;
        }
    }

    // "M"-part of the ModRm. This may be (i) a register, (ii) a memory address
    // or (iii) a scale-index-base memory access.
    Value *_mv;
    // "R"-part of the ModRm. This my be (i) a register or (ii) an extension of the opcode.
    Value *_rv;
    int _xop;
};

void MachineCodeEmitter::run() {
    auto textString = _elf->addString(std::make_unique<elf::String>(".text"));
    auto gotString = _elf->addString(std::make_unique<elf::String>(".got"));
    auto pltString = _elf->addString(std::make_unique<elf::String>(".plt"));
    auto symbolString = _elf->addString(std::make_unique<elf::String>(_fn->name));

    auto textSection = _elf->insertFragment(std::make_unique<elf::ByteSection>());
    textSection->name = textString;
    textSection->type = SHT_PROGBITS;
    textSection->flags = SHF_ALLOC | SHF_EXECINSTR;

    auto symbol = _elf->addSymbol(std::make_unique<elf::Symbol>());
    symbol->name = symbolString;
    symbol->section = textSection;

    _gotSection = _elf->insertFragment(std::make_unique<elf::ByteSection>());
    _gotSection->name = gotString;
    _gotSection->type = SHT_PROGBITS;
    _gotSection->flags = SHF_ALLOC;

    _pltSection = _elf->insertFragment(std::make_unique<elf::ByteSection>());
    _pltSection->name = pltString;
    _pltSection->type = SHT_PROGBITS;
    _pltSection->flags = SHF_ALLOC | SHF_EXECINSTR;

    // Generate a symbol for each basic block.
    size_t i = 0;
    for (auto bb : _fn->blocks()) {
        auto bbString = _elf->addString(std::make_unique<elf::String>(_fn->name
                + ".bb" + std::to_string(i)));
        auto bbSymbol = _elf->addSymbol(std::make_unique<elf::Symbol>());
        bbSymbol->name = bbString;
        bbSymbol->section = textSection;
        _bbSymbols.insert({bb, bbSymbol});
        i++;
    }

    for (auto bb : _fn->blocks()) {
        auto bbSymbol = _bbSymbols.at(bb);
        bbSymbol->value = textSection->buffer.size();
        _emitBlock(bb, textSection);
    }
}

void MachineCodeEmitter::_emitBlock(BasicBlock *bb, elf::ByteSection *textSection) {
    util::ByteEncoder text{&textSection->buffer};
    util::ByteEncoder got{&_gotSection->buffer};
    util::ByteEncoder plt{&_pltSection->buffer};

    for (auto inst : bb->instructions()) {
        if (auto nop = hierarchy_cast<NopInstruction *>(inst); nop) {
            // Do not emit any code.
        }else if (hierarchy_cast<DefineOffsetInstruction *>(inst)) {
            // Do not emit any code.
        } else if (auto pushSave = hierarchy_cast<PushSaveInstruction *>(inst); pushSave) {
            assert(pushSave->operandRegister >= 0);
            if (pushSave->operandRegister < 8) {
                encode8(text, 0x50 + pushSave->operandRegister);
            } else {
                encodeRawRex(text, OperandSize::dword, 0, 0, 1);
                encode8(text, 0xFF);
                encodeRawModRm(text, 3, pushSave->operandRegister & 7, 6);
            }
        } else if (auto popRestore = hierarchy_cast<PopRestoreInstruction *>(inst); popRestore) {
            assert(popRestore->operandRegister >= 0);
            if (popRestore->operandRegister < 8) {
                encode8(text, 0x58 + popRestore->operandRegister);
            } else {
                encodeRawRex(text, OperandSize::dword, 0, 0, 1);
                encode8(text, 0x8F);
                encodeRawModRm(text, 3, popRestore->operandRegister & 7, 0);
            }
        } else if (auto movMC = hierarchy_cast<MovMCInstruction *>(inst); movMC) {
            auto rr = getRegister(movMC->result.get());
            assert(rr >= 0);
            if (rr < 8) {
                encode8(text, 0xB8 + rr);
            } else {
                ModRmEncoding modRm{movMC->result.get(), 0};
                modRm.encodeRex(text);
                encode8(text, 0xC7);
                modRm.encodeModRmSib(text);
            }
            encode32(text, movMC->value);
        } else if (auto movMR = hierarchy_cast<MovMRInstruction *>(inst); movMR) {
            ModRmEncoding modRm{movMR->result.get(), movMR->operand.get()};
            modRm.encodeRex(text);
            encode8(text, 0x89);
            modRm.encodeModRmSib(text);
        } else if (auto movRM = hierarchy_cast<MovRMInstruction *>(inst); movRM) {
            ModRmEncoding modRm{movRM->operand.get(), movRM->result.get()};
            modRm.encodeRex(text);
            encode8(text, 0x8B);
            modRm.encodeModRmSib(text);
        } else if (auto xchgMR = hierarchy_cast<XchgMRInstruction *>(inst); xchgMR) {
            ModRmEncoding modRm{xchgMR->firstResult.get(), xchgMR->secondResult.get()};
            modRm.encodeRex(text);
            encode8(text, 0x87);
            modRm.encodeModRmSib(text);
        } else if (auto negM = hierarchy_cast<NegMInstruction *>(inst); negM) {
            ModRmEncoding modRm{negM->result.get(), 3};
            modRm.encodeRex(text);
            encode8(text, 0xF7);
            modRm.encodeModRmSib(text);
        } else if (auto addMR = hierarchy_cast<AddMRInstruction *>(inst); addMR) {
            ModRmEncoding modRm{addMR->result.get(), addMR->secondary.get()};
            modRm.encodeRex(text);
            encode8(text, 0x01);
            modRm.encodeModRmSib(text);
        } else if (auto andMR = hierarchy_cast<AndMRInstruction *>(inst); andMR) {
            ModRmEncoding modRm{andMR->result.get(), andMR->secondary.get()};
            modRm.encodeRex(text);
            encode8(text, 0x21);
            modRm.encodeModRmSib(text);
        }else if (auto call = hierarchy_cast<CallInstruction *>(inst); call) {
            auto string = _elf->addString(std::make_unique<elf::String>(call->function));
            auto symbol = _elf->addSymbol(std::make_unique<elf::Symbol>());
            symbol->name = string;

            // Add a GOT entry for the function.
            // TODO: Create the "special" GOT entries.
            // TODO: Move GOT creation into the InternalLinkPass.
            auto gotString = _elf->addString(std::make_unique<elf::String>(call->function + "@got"));
            auto gotSymbol = _elf->addSymbol(std::make_unique<elf::Symbol>());
            gotSymbol->name = gotString;
            gotSymbol->section = _gotSection;
            gotSymbol->value = got.offset();

            auto jumpSlot = _elf->addRelocation(std::make_unique<elf::Relocation>());
            jumpSlot->section = _gotSection;
            jumpSlot->offset = got.offset();
            jumpSlot->symbol = symbol;
            encode64(got, 0);

            // Add a PLT stub for the entry.
            // TODO: Create the PLT header (and correct entries) for dynamic binding.
            // TODO: Properly align PLT entries as in the ABI supplement.
            // TODO: Move PLT creation into the InternalLinkPass.
            auto pltString = _elf->addString(std::make_unique<elf::String>(call->function + "@plt"));
            auto pltSymbol = _elf->addSymbol(std::make_unique<elf::Symbol>());
            pltSymbol->name = pltString;
            pltSymbol->section = _pltSection;
            pltSymbol->value = plt.offset();

            auto jumpThroughGot = _elf->addInternalRelocation(std::make_unique<elf::Relocation>());
            jumpThroughGot->section = _pltSection;
            jumpThroughGot->offset = plt.offset() + 2;
            jumpThroughGot->symbol = gotSymbol;
            jumpThroughGot->addend = -4;

            encode8(plt, 0xFF);
            encode8(plt, 0x25); // TODO: Use encodeRawModRm().
            encode32(plt, 0);

            // Add the actual jump to the .text section.
            auto jumpToPlt = _elf->addInternalRelocation(std::make_unique<elf::Relocation>());
            jumpToPlt->section = textSection;
            jumpToPlt->offset = text.offset() + 1;
            jumpToPlt->symbol = pltSymbol;
            jumpToPlt->addend = -4;

            encode8(text, 0xE8);
            encode32(text, 0); // Relocation points here.
        } else {
            assert(!"Unexpected x86_64 IR instruction");
        }
    }

    auto branch = bb->branch();
    if (auto ret = hierarchy_cast<RetBranch *>(branch); ret) {
        encode8(text, 0xC3);
    } else if (auto jmp = hierarchy_cast<JmpBranch *>(branch); jmp) {
        auto jump = _elf->addInternalRelocation(std::make_unique<elf::Relocation>());
        jump->section = textSection;
        jump->offset = text.offset() + 1;
        jump->symbol = _bbSymbols.at(jmp->target);
        jump->addend = -4;

        encode8(text, 0xE9);
        encode32(text, 0);
    } else if (auto jnz = hierarchy_cast<JnzBranch *>(branch); jnz) {
        ModRmEncoding modRm{jnz->operand.get(), jnz->operand.get()};
        modRm.encodeRex(text);
        encode8(text, 0x85);
        modRm.encodeModRmSib(text);

        auto ifJump = _elf->addInternalRelocation(std::make_unique<elf::Relocation>());
        ifJump->section = textSection;
        ifJump->offset = text.offset() + 2;
        ifJump->symbol = _bbSymbols.at(jnz->ifTarget);
        ifJump->addend = -4;

        encode8(text, 0x0F);
        encode8(text, 0x85);
        encode32(text, 0);

        auto elseJump = _elf->addInternalRelocation(std::make_unique<elf::Relocation>());
        elseJump->section = textSection;
        elseJump->offset = text.offset() + 1;
        elseJump->symbol = _bbSymbols.at(jnz->elseTarget);
        elseJump->addend = -4;

        encode8(text, 0xE9);
        encode32(text, 0);
    } else {
        assert(!"Unexpected x86_64 IR branch");
    }
}

} // namespace lewis::targets::x86_64
