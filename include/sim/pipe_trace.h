#pragma once

#include "common/types.h"

#include <cstdint>
#include <fstream>
#include <string>

namespace e203sim {

class pipe_trace
{
public:
    static pipe_trace& instance();

    void enable(const std::string& path, uint32_t cycle_ns);
    void disable();
    bool enabled() const;

    uint64_t next_seq();
    uint64_t now_tick() const;
    uint64_t cycle_tick() const;

    void emit_instruction(uint64_t seq,
                          addr_t pc,
                          uint32_t inst,
                          uint64_t fetch_tick,
                          uint64_t decode_tick,
                          uint64_t dispatch_tick,
                          uint64_t issue_tick,
                          uint64_t complete_tick,
                          uint64_t retire_tick,
                          const std::string& disasm);

    void emit_detail(uint64_t tick,
                     const std::string& source,
                     uint64_t seq,
                     const std::string& message);

private:
    pipe_trace() = default;

    std::ofstream file_;
    bool enabled_ = false;
    uint64_t next_seq_ = 0;
    uint64_t cycle_tick_ = 10000;
};

} // namespace e203sim

