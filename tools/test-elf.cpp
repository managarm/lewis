
#include <stdexcept>
#include <lewis/elf/object.hpp>
#include <lewis/elf/passes.hpp>
#include <lewis/target-x86_64/mc-emitter.hpp>

int main() {
    lewis::BasicBlock bb;
    bb.insertInstruction(std::make_unique<lewis::targets::x86_64::LoadConstCode>());

    lewis::elf::Object elf;
    lewis::targets::x86_64::MachineCodeEmitter mce{&bb, &elf};
    mce.run();

    // Create headers, layout the file, finalize headers.
    auto headers_pass = lewis::elf::CreateHeadersPass::create(&elf);
    auto layout_pass = lewis::elf::LayoutPass::create(&elf);
    auto commit_pass = lewis::elf::CommitHeadersPass::create(&elf);
    headers_pass->run();
    layout_pass->run();
    commit_pass->run();

    // Write the output file.
    FILE *stream;
    if(!(stream = fopen("a.out", "wb")))
        throw std::runtime_error("Could not open output file");
    elf.emitTo(stream);
    fclose(stream);
}

