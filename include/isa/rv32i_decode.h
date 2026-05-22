#pragma once

#include "common/types.h"

#include <cstdint>

namespace e203sim {

// EXU 使用的粗粒度指令类别。这里按执行通路划分，而不是按 ISA opcode
// 原样暴露，便于 dispatch 直接选择 ALU、LSU、branch/jump、CSR/commit 路径。
enum class inst_kind {
    Illegal,
    Alu,
    Load,
    Store,
    Branch,
    Jal,
    Jalr,
    Lui,
    Auipc,
    Csr,
    MulDiv,
    Ecall,
    Ebreak,
    Mret,
    Fence,
};

// RV32I ALU 子操作，覆盖寄存器型和立即数型算术/逻辑指令。
enum class alu_op {
    Add,
    Sub,
    And,
    Or,
    Xor,
    Sll,
    Srl,
    Sra,
    Slt,
    Sltu,
    PassB,
};

// 分支比较类型。None 用于非分支指令的默认值。
enum class branch_op {
    None,
    Beq,
    Bne,
    Blt,
    Bge,
    Bltu,
    Bgeu,
};

// machine CSR v1 支持的 CSR 指令操作类型。
enum class csr_op {
    None,
    Csrrw,
    Csrrs,
    Csrrc,
    Csrrwi,
    Csrrsi,
    Csrrci,
};

// RV32M 乘除法子操作。执行单元按 e203 shared MULDIV 的外部可见行为建模：
// EXU 内阻塞式多周期完成，不进入 OITF。
enum class muldiv_op {
    None,
    Mul,
    Mulh,
    Mulhsu,
    Mulhu,
    Div,
    Divu,
    Rem,
    Remu,
};

// decode 输出的规范化控制包。
// - rd/rs1/rs2 保存架构寄存器编号。
// - *_read/rd_write 用于 EXU 与 OITF 做 RAW/WAW hazard 判断。
// - imm 已按对应指令格式完成符号扩展或 U-type 高位对齐。
// - mem_size/mem_unsigned 描述 LSU 访问宽度和 load 扩展方式。
// - illegal 为 true 时，其他控制字段只作为诊断上下文，不参与执行。
struct decoded_inst {
    inst_kind kind = inst_kind::Illegal;
    alu_op alu = alu_op::Add;
    branch_op branch = branch_op::None;
    csr_op csr = csr_op::None;
    muldiv_op muldiv = muldiv_op::None;
    uint8_t rd = 0;
    uint8_t rs1 = 0;
    uint8_t rs2 = 0;
    uint32_t imm = 0;
    uint16_t csr_addr = 0;
    AccessSize mem_size = AccessSize::Word;
    bool mem_unsigned = false;
    bool rd_write = false;
    bool rs1_read = false;
    bool rs2_read = false;
    bool illegal = true;
};

// 解码当前模型支持的 RV32I/RV32M 32-bit 指令。压缩指令在 v1 EXU 中作为
// illegal 处理，后续若加入 C extension，可在这里扩展 instr_32bit=false 的路径。
decoded_inst decode_rv32i(uint32_t instr, bool instr_32bit = true);

} // namespace e203sim
