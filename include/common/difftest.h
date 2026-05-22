#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace e203sim {

enum diff_direction : bool {
    DIFFTEST_TO_DUT = false,
    DIFFTEST_TO_REF = true,
};

struct diff_context {
    std::array<uint32_t, 32> gpr{};
    uint32_t pc = 0;
};

struct diff_store_event {
    uint32_t vaddr = 0;
    uint32_t data = 0;
    uint8_t len = 0;
    uint8_t type = 0;
};

struct diff_retire_event {
    uint32_t pc = 0;
    uint32_t next_pc = 0;
    uint32_t instr = 0;
    diff_context regs{};
    diff_store_event store{};
    bool has_store = false;
    bool skip_ref = false;
    std::string skip_reason;
};

} // namespace e203sim

