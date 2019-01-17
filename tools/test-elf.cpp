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

    auto p0 = b0->attachPhi(std::make_unique<lewis::ArgumentPhi>());
    auto v0 = p0->value.setNew<lewis::LocalValue>();
    auto i1 = b0->insertInstruction(std::make_unique<lewis::LoadOffsetInstruction>(
            v0, 4));
    auto v1 = i1->result.setNew<lewis::LocalValue>();
    b0->setBranch(std::make_unique<lewis::UnconditionalBranch>(b1));

    auto p1 = b1->attachPhi(std::make_unique<lewis::DataFlowPhi>());
    p1->attachNewEdge(b0, v1);
    auto v2 = p1->value.setNew<lewis::LocalValue>();
    auto i2 = b1->insertInstruction(std::make_unique<lewis::UnaryMathInstruction>(
            lewis::UnaryMathOpcode::negate, v2));
    i2->result.setNew<lewis::LocalValue>();
    b1->setBranch(std::make_unique<lewis::FunctionReturnBranch>());

    for (auto bb : f0.blocks()) {
        auto lo = lewis::targets::x86_64::LowerCodePass::create(bb);
        lo->run();
    }
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
