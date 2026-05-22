#pragma once

#include "common/types.h"
#include "isa/rv32i_decode.h"

#include <cstdint>

class alu_unit
{
public:
    // 纯组合 ALU 计算：调用者负责选择 lhs/rhs 来源和写回使能。
    e203sim::data_t execute(e203sim::alu_op op,
                            e203sim::data_t lhs,
                            e203sim::data_t rhs) const;
    // 分支比较单元：只返回 taken，不计算目标 PC。
    bool branch_taken(e203sim::branch_op op,
                      e203sim::data_t lhs,
                      e203sim::data_t rhs) const;
};
