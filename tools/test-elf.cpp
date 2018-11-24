
#include <stdexcept>
#include <lewis/elf/object.hpp>
#include <lewis/elf/passes.hpp>

int main() {
    lewis::elf::Object elf;

    // Create some section with filler contents.
    auto section = std::make_unique<lewis::elf::Section>();
    section->buffer.push_back(0xDE);
    section->buffer.push_back(0xAD);
    section->buffer.push_back(0xBE);
    section->buffer.push_back(0xEF);
    elf.insertFragment(std::move(section));

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

