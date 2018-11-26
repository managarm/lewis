
#pragma once

#include <lewis/elf/object.hpp>

namespace lewis::elf {

struct FileEmitter {
    static std::unique_ptr<FileEmitter> create(Object *elf);

    virtual ~FileEmitter() = default;

    virtual void run() = 0;

    std::vector<uint8_t> buffer;
};

} // namespace lewis::elf

