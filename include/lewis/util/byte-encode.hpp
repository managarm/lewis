// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#pragma once

#include <cstring>

namespace lewis::util {

struct ByteEncoder {
    ByteEncoder(std::vector<uint8_t> *out)
    : _out{out} { }

private:
    template<typename T>
    void _poke(T v) {
        auto n = _out->size();
        _out->resize(n + sizeof(T));
        memcpy(_out->data() + n, &v, sizeof(T));
    }

public:
    size_t offset() {
        return _out->size();
    }

    friend void encodeChars(ByteEncoder &e, const char *v) {
        while (*v)
            e._poke<uint8_t>(*(v++));
    }
    friend void encode8(ByteEncoder &e, uint8_t v) { e._poke<uint8_t>(v); }
    friend void encode16(ByteEncoder &e, uint16_t v) { e._poke<uint16_t>(v); }
    friend void encode32(ByteEncoder &e, uint32_t v) { e._poke<uint32_t>(v); }
    friend void encode64(ByteEncoder &e, uint64_t v) { e._poke<uint64_t>(v); }

private:
    std::vector<uint8_t> *_out;
};

} // namespace lewis::util
