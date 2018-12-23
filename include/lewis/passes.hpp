
#pragma once

#include <lewis/ir.hpp>

namespace lewis {

struct BasicBlockPass {
    virtual ~BasicBlockPass() = default;

    virtual void run() = 0;
};

} // namespace lewis::elf

