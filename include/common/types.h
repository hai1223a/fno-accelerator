#pragma once
#include <cstdint>
namespace e203sim {

using addr_t = uint32_t;
using data_t = uint32_t;
using byte_t = uint8_t;

enum class AccessSize {
    Byte = 1,
    HalfWord = 2,
    Word = 4,
};

} // namespace e203sim