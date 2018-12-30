// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <stdexcept>
#include <lewis/elf/object.hpp>
#include <lewis/elf/passes.hpp>
#include <lewis/elf/file-emitter.hpp>
#include <lewis/target-x86_64/arch-passes.hpp>
#include <lewis/target-x86_64/mc-emitter.hpp>

int main() {
    lewis::BasicBlock bb;
    auto i0 = bb.insertInstruction(std::make_unique<lewis::LoadConstInstruction>(42));
    auto i1 = bb.insertInstruction(std::make_unique<lewis::UnaryMathInstruction>(
            lewis::UnaryMathOpcode::negate, i0->result()));
    bb.setBranch(std::make_unique<lewis::UnconditionalBranch>());
    (void)i1;

    auto lower = lewis::targets::x86_64::LowerCodePass::create(&bb);
    auto ra = lewis::targets::x86_64::AllocateRegistersPass::create(&bb);
    lower->run();
    ra->run();

    lewis::elf::Object elf;
    lewis::targets::x86_64::MachineCodeEmitter mce{&bb, &elf};
    mce.run();

    // Create headers and layout the file.
    auto headers_pass = lewis::elf::CreateHeadersPass::create(&elf);
    auto layout_pass = lewis::elf::LayoutPass::create(&elf);
    headers_pass->run();
    layout_pass->run();

    // Compose the output file.
    auto file_emitter = lewis::elf::FileEmitter::create(&elf);
    file_emitter->run();

    // Write the output file.
    FILE *stream;
    if(!(stream = fopen("a.out", "wb")))
        throw std::runtime_error("Could not open output file");
    auto written = fwrite(file_emitter->buffer.data(), sizeof(uint8_t),
        file_emitter->buffer.size(), stream);
    if(written != file_emitter->buffer.size())
        throw std::runtime_error("Could not write buffer to FILE");

    fclose(stream);
}
