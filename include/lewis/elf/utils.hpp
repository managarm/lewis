// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#pragma once

#include <cstring>
#include <lewis/util/byte-encode.hpp>

namespace lewis::elf {

inline void encodeAddr(util::ByteEncoder &enc, uint64_t v) { encode64(enc, v); }
inline void encodeOff(util::ByteEncoder &enc, uint64_t v) { encode64(enc, v); }
inline void encodeHalf(util::ByteEncoder &enc, uint16_t v) { encode16(enc, v); }
inline void encodeWord(util::ByteEncoder &enc, uint32_t v) { encode32(enc, v); }
inline void encodeXword(util::ByteEncoder &enc, uint64_t v) { encode64(enc, v); }

} // namespace lewis::elf