// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <vector>
#include <lewis/elf/object.hpp>
#include <lewis/target-x86_64/arch-ir.hpp>
#include <lewis/util/byte-encode.hpp>

namespace lewis::targets::x86_64 {

// TODO: This should probably also use pimpl.
struct MachineCodeEmitter {
    MachineCodeEmitter(Function *fn, elf::Object *elf);

    void run();

private:
    void _emitBlock(BasicBlock *bb, elf::ByteSection *textSection);

    Function *_fn;
    elf::Object *_elf;
    elf::ByteSection *_gotSection = nullptr;
    elf::ByteSection *_pltSection = nullptr;
};

} // namespace lewis::targets::x86_64
