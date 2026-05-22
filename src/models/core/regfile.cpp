#include "models/core/regfile.h"

e203sim::data_t regfile::read(uint8_t idx) const
{
    // x0 恒为 0；idx 做 5-bit mask，避免测试误传越界值访问数组外。
    return idx == 0 ? 0 : regs_[idx & 0x1fu];
}

void regfile::write(uint8_t idx, e203sim::data_t data, bool wen)
{
    // RISC-V x0 写入被丢弃。末尾再次写 0，抵御非预期状态污染。
    if (wen && idx != 0) {
        regs_[idx & 0x1fu] = data;
    }
    regs_[0] = 0;
}

std::array<e203sim::data_t, 32> regfile::snapshot() const
{
    auto regs = regs_;
    regs[0] = 0;
    return regs;
}

void regfile::reset()
{
    regs_.fill(0);
}
