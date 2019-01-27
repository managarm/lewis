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
    f0.name = "automate_irq";
    auto b0 = f0.addBlock(std::make_unique<lewis::BasicBlock>());
    auto arg0 = b0->attachPhi(std::make_unique<lewis::ArgumentPhi>());
    auto v0 = arg0->value.setNew<lewis::LocalValue>();
    v0->setType(lewis::globalPointerType());

    auto i1 = b0->insertNewInstruction<lewis::LoadOffsetInstruction>(v0, 0);
    auto v1 = i1->result.setNew<lewis::LocalValue>();
    v1->setType(lewis::globalPointerType());

    auto i2 = b0->insertNewInstruction<lewis::LoadOffsetInstruction>(v0, 8);
    auto v2 = i2->result.setNew<lewis::LocalValue>();
    v2->setType(lewis::globalInt32Type());

    auto i3 = b0->insertNewInstruction<lewis::LoadConstInstruction>(4);
    auto v3 = i3->result.setNew<lewis::LocalValue>();
    v3->setType(lewis::globalInt32Type());

    auto i4 = b0->insertNewInstruction<lewis::BinaryMathInstruction>(
            lewis::BinaryMathOpcode::add, v2, v3);
    auto v4 = i4->result.setNew<lewis::LocalValue>();
    v4->setType(lewis::globalInt32Type());

    auto i5 = b0->insertNewInstruction<lewis::InvokeInstruction>("__mmio_read32", 2);
    i5->operand(0) = v1;
    i5->operand(1) = v4;
    auto v5 = i5->result.setNew<lewis::LocalValue>();
    v5->setType(lewis::globalInt32Type());

    auto i6 = b0->insertNewInstruction<lewis::LoadConstInstruction>(23);
    auto v6 = i6->result.setNew<lewis::LocalValue>();
    v6->setType(lewis::globalInt32Type());

    auto i7 = b0->insertNewInstruction<lewis::BinaryMathInstruction>(
            lewis::BinaryMathOpcode::bitwiseAnd, v5, v6);
    auto v7 = i7->result.setNew<lewis::LocalValue>();
    v7->setType(lewis::globalInt32Type());

    auto br0 = b0->setBranch(std::make_unique<lewis::FunctionReturnBranch>(1));
    br0->operand(0) = v7;

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
    auto link_pass = lewis::elf::InternalLinkPass::create(&elf);
    headers_pass->run();
    layout_pass->run();
    link_pass->run();

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
