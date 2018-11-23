
#include <stdexcept>
#include <lewis/elf/object.hpp>

int main() {
    lewis::elf::Object elf;

    FILE *stream;
    if(!(stream = fopen("a.out", "wb")))
        throw std::runtime_error("Could not open output file");
    elf.emitTo(stream);
    fclose(stream);
}

