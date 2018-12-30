// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <lewis/passes.hpp>

namespace lewis::targets::x86_64 {

// The following passes are implemented using Pimpl.

// Lower code from generic IR to x86 IR.
struct LowerCodePass : BasicBlockPass {
    static std::unique_ptr<LowerCodePass> create(BasicBlock *bb);
};

// Allocate registers in x86 IR.
struct AllocateRegistersPass : FunctionPass {
    static std::unique_ptr<AllocateRegistersPass> create(Function *fn);
};

} // namespace lewis::targets::x86_64
