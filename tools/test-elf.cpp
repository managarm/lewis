// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <stdexcept>
#include <lewis/elf/object.hpp>
#include <lewis/elf/passes.hpp>
#include <lewis/elf/file-emitter.hpp>
#include <lewis/target-x86_64/arch-passes.hpp>
#include <lewis/target-x86_64/mc-emitter.hpp>

int main() {
    lewis::Function f0;
    auto b0 = f0.addBlock(std::make_unique<lewis::BasicBlock>());
    auto b1 = f0.addBlock(std::make_unique<lewis::BasicBlock>());

    auto i0 = b0->insertInstruction(std::make_unique<lewis::LoadConstInstruction>(42));
    b0->setBranch(std::make_unique<lewis::UnconditionalBranch>());

    auto p0 = b1->attachPhi(std::make_unique<lewis::PhiNode>());
    auto e0 = p0->attachEdge(std::make_unique<lewis::PhiEdge>(b0, i0->result()));
    auto i1 = b1->insertInstruction(std::make_unique<lewis::UnaryMathInstruction>(
            lewis::UnaryMathOpcode::negate, p0));
    (void)i1;
    b1->setBranch(std::make_unique<lewis::FunctionReturnBranch>());

    auto lo0 = lewis::targets::x86_64::LowerCodePass::create(b0);
    auto lo1 = lewis::targets::x86_64::LowerCodePass::create(b1);
    lo1->run();
    lo0->run();
    auto ra = lewis::targets::x86_64::AllocateRegistersPass::create(&f0);
    ra->run();

    lewis::elf::Object elf;
    lewis::targets::x86_64::MachineCodeEmitter mce{&f0, &elf};
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
