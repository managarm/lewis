
#pragma once

#include <cstdint>
#include <vector>
#include <lewis/elf/object.hpp>
#include <lewis/target-x86_64/arch-ir.hpp>

namespace lewis::targets::x86_64 {

struct MachineCodeEmitter {
    MachineCodeEmitter(BasicBlock *bb, elf::Object *elf);

    void run();

private:
    BasicBlock *_bb;
    elf::Object *_elf;
};

} // namespace lewis::targets::x86_64

