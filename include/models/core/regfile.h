#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>

class regfile
{
public:
    // 读架构寄存器。x0 强制返回 0，匹配 RISC-V 语义。
    e203sim::data_t read(uint8_t idx) const;
    // 写架构寄存器。wen=false 或 rd=x0 时不改变可见状态。
    void write(uint8_t idx, e203sim::data_t data, bool wen);
    std::array<e203sim::data_t, 32> snapshot() const;
    // 清空全部寄存器，主要用于测试或复位扩展点。
    void reset();

private:
    std::array<e203sim::data_t, 32> regs_{};
};
