
#pragma once

#include <memory>
#include <optional>
#include <vector>

namespace lewis::elf {

struct Object {
    void emitTo(FILE *stream);
};

} // namespace lewis::elf

