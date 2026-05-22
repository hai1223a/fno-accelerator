#include "models/core/alu_unit.h"

e203sim::data_t alu_unit::execute(e203sim::alu_op op,
                                  e203sim::data_t lhs,
                                  e203sim::data_t rhs) const
{
    // RV32 shift 指令只使用 rhs[4:0] 作为 shamt。
    const uint32_t shamt = rhs & 0x1fu;
    switch (op) {
    case e203sim::alu_op::Add: return lhs + rhs;
    case e203sim::alu_op::Sub: return lhs - rhs;
    case e203sim::alu_op::And: return lhs & rhs;
    case e203sim::alu_op::Or: return lhs | rhs;
    case e203sim::alu_op::Xor: return lhs ^ rhs;
    case e203sim::alu_op::Sll: return lhs << shamt;
    case e203sim::alu_op::Srl: return lhs >> shamt;
    case e203sim::alu_op::Sra:
        return static_cast<uint32_t>(static_cast<int32_t>(lhs) >> shamt);
    case e203sim::alu_op::Slt:
        return static_cast<int32_t>(lhs) < static_cast<int32_t>(rhs) ? 1u : 0u;
    case e203sim::alu_op::Sltu:
        return lhs < rhs ? 1u : 0u;
    case e203sim::alu_op::PassB:
        return rhs;
    }
    return 0;
}

bool alu_unit::branch_taken(e203sim::branch_op op,
                            e203sim::data_t lhs,
                            e203sim::data_t rhs) const
{
    // 有符号分支使用 int32_t 解释操作数，无符号分支保留 uint32_t 比较。
    switch (op) {
    case e203sim::branch_op::None: return false;
    case e203sim::branch_op::Beq: return lhs == rhs;
    case e203sim::branch_op::Bne: return lhs != rhs;
    case e203sim::branch_op::Blt:
        return static_cast<int32_t>(lhs) < static_cast<int32_t>(rhs);
    case e203sim::branch_op::Bge:
        return static_cast<int32_t>(lhs) >= static_cast<int32_t>(rhs);
    case e203sim::branch_op::Bltu: return lhs < rhs;
    case e203sim::branch_op::Bgeu: return lhs >= rhs;
    }
    return false;
}
