#include "isa/rv32i_decode.h"

namespace e203sim {

namespace {

// 取闭区间 bit slice，decode 表统一用 hi/lo 表达 ISA 字段位置。
uint32_t bits(uint32_t value, unsigned int hi, unsigned int lo)
{
    const uint32_t width = hi - lo + 1u;
    return (value >> lo) & ((1u << width) - 1u);
}

// 将 ISA immediate 原始字段符号扩展为 RV32 数据宽度。
uint32_t sign_extend(uint32_t value, unsigned int bits_count)
{
    const uint32_t sign = 1u << (bits_count - 1u);
    return (value ^ sign) - sign;
}

// I/S/B/U/J 各格式 immediate 拼接函数；返回值均已是 EXU 可直接使用的
// 32-bit immediate，其中 B/J immediate 保留 bit0=0 的分支跳转粒度。
uint32_t imm_i(uint32_t instr)
{
    return sign_extend(bits(instr, 31, 20), 12);
}

uint32_t imm_s(uint32_t instr)
{
    return sign_extend((bits(instr, 31, 25) << 5u) | bits(instr, 11, 7), 12);
}

uint32_t imm_b(uint32_t instr)
{
    const uint32_t raw = (bits(instr, 31, 31) << 12u) |
                         (bits(instr, 7, 7) << 11u) |
                         (bits(instr, 30, 25) << 5u) |
                         (bits(instr, 11, 8) << 1u);
    return sign_extend(raw, 13);
}

uint32_t imm_u(uint32_t instr)
{
    return instr & 0xfffff000u;
}

uint32_t imm_j(uint32_t instr)
{
    const uint32_t raw = (bits(instr, 31, 31) << 20u) |
                         (bits(instr, 19, 12) << 12u) |
                         (bits(instr, 20, 20) << 11u) |
                         (bits(instr, 30, 21) << 1u);
    return sign_extend(raw, 21);
}

decoded_inst base(uint32_t instr)
{
    decoded_inst dec;
    // rd/rs1/rs2/CSR 地址位置在相关格式中固定，先统一抽取。
    dec.rd = static_cast<uint8_t>(bits(instr, 11, 7));
    dec.rs1 = static_cast<uint8_t>(bits(instr, 19, 15));
    dec.rs2 = static_cast<uint8_t>(bits(instr, 24, 20));
    dec.csr_addr = static_cast<uint16_t>(bits(instr, 31, 20));
    return dec;
}

} // namespace

decoded_inst decode_rv32i(uint32_t instr, bool instr_32bit)
{
    decoded_inst dec = base(instr);
    if (!instr_32bit) {
        // v1 EXU 不执行 C extension；IFU 可传递 16-bit packet，但这里提交 illegal。
        dec.kind = inst_kind::Illegal;
        dec.illegal = true;
        return dec;
    }

    const uint32_t opcode = bits(instr, 6, 0);
    const uint32_t funct3 = bits(instr, 14, 12);
    const uint32_t funct7 = bits(instr, 31, 25);
    dec.illegal = false;

    // 主 decode 表按 opcode 分类，再用 funct3/funct7 细分到执行单元操作。
    switch (opcode) {
    case 0x33: // OP
        // R-type OP：funct7=0x01 归入 RV32M，其余按 RV32I ALU 解码。
        dec.kind = inst_kind::Alu;
        dec.rs1_read = true;
        dec.rs2_read = true;
        dec.rd_write = true;
        if (funct7 == 0x01) {
            dec.kind = inst_kind::MulDiv;
            switch (funct3) {
            case 0x0: dec.muldiv = muldiv_op::Mul; break;
            case 0x1: dec.muldiv = muldiv_op::Mulh; break;
            case 0x2: dec.muldiv = muldiv_op::Mulhsu; break;
            case 0x3: dec.muldiv = muldiv_op::Mulhu; break;
            case 0x4: dec.muldiv = muldiv_op::Div; break;
            case 0x5: dec.muldiv = muldiv_op::Divu; break;
            case 0x6: dec.muldiv = muldiv_op::Rem; break;
            case 0x7: dec.muldiv = muldiv_op::Remu; break;
            default: dec.illegal = true; break;
            }
            break;
        }
        switch (funct3) {
        case 0x0:
            if (funct7 == 0x00) dec.alu = alu_op::Add;
            else if (funct7 == 0x20) dec.alu = alu_op::Sub;
            else dec.illegal = true;
            break;
        case 0x1: dec.alu = alu_op::Sll; dec.illegal = funct7 != 0x00; break;
        case 0x2: dec.alu = alu_op::Slt; dec.illegal = funct7 != 0x00; break;
        case 0x3: dec.alu = alu_op::Sltu; dec.illegal = funct7 != 0x00; break;
        case 0x4: dec.alu = alu_op::Xor; dec.illegal = funct7 != 0x00; break;
        case 0x5:
            if (funct7 == 0x00) dec.alu = alu_op::Srl;
            else if (funct7 == 0x20) dec.alu = alu_op::Sra;
            else dec.illegal = true;
            break;
        case 0x6: dec.alu = alu_op::Or; dec.illegal = funct7 != 0x00; break;
        case 0x7: dec.alu = alu_op::And; dec.illegal = funct7 != 0x00; break;
        default: dec.illegal = true; break;
        }
        break;
    case 0x13: // OP-IMM
        // I-type ALU：rs1 与立即数计算。移位立即数只取 shamt 字段。
        dec.kind = inst_kind::Alu;
        dec.rs1_read = true;
        dec.rd_write = true;
        dec.imm = imm_i(instr);
        switch (funct3) {
        case 0x0: dec.alu = alu_op::Add; break;
        case 0x2: dec.alu = alu_op::Slt; break;
        case 0x3: dec.alu = alu_op::Sltu; break;
        case 0x4: dec.alu = alu_op::Xor; break;
        case 0x6: dec.alu = alu_op::Or; break;
        case 0x7: dec.alu = alu_op::And; break;
        case 0x1:
            dec.alu = alu_op::Sll;
            dec.imm = bits(instr, 24, 20);
            dec.illegal = funct7 != 0x00;
            break;
        case 0x5:
            dec.imm = bits(instr, 24, 20);
            if (funct7 == 0x00) dec.alu = alu_op::Srl;
            else if (funct7 == 0x20) dec.alu = alu_op::Sra;
            else dec.illegal = true;
            break;
        default: dec.illegal = true; break;
        }
        break;
    case 0x03: // LOAD
        // LOAD：EXU 计算地址，LSU 负责 data_length/byte-enable 与扩展返回。
        dec.kind = inst_kind::Load;
        dec.rs1_read = true;
        dec.rd_write = true;
        dec.imm = imm_i(instr);
        switch (funct3) {
        case 0x0: dec.mem_size = AccessSize::Byte; break;
        case 0x1: dec.mem_size = AccessSize::HalfWord; break;
        case 0x2: dec.mem_size = AccessSize::Word; break;
        case 0x4: dec.mem_size = AccessSize::Byte; dec.mem_unsigned = true; break;
        case 0x5: dec.mem_size = AccessSize::HalfWord; dec.mem_unsigned = true; break;
        default: dec.illegal = true; break;
        }
        break;
    case 0x23: // STORE
        // STORE：rs1 提供 base，rs2 提供写数据，S-type immediate 提供 offset。
        dec.kind = inst_kind::Store;
        dec.rs1_read = true;
        dec.rs2_read = true;
        dec.imm = imm_s(instr);
        switch (funct3) {
        case 0x0: dec.mem_size = AccessSize::Byte; break;
        case 0x1: dec.mem_size = AccessSize::HalfWord; break;
        case 0x2: dec.mem_size = AccessSize::Word; break;
        default: dec.illegal = true; break;
        }
        break;
    case 0x63: // BRANCH
        // BRANCH：decode 只给出比较类型和目标 offset，EXU 结合预测结果决定 flush。
        dec.kind = inst_kind::Branch;
        dec.rs1_read = true;
        dec.rs2_read = true;
        dec.imm = imm_b(instr);
        switch (funct3) {
        case 0x0: dec.branch = branch_op::Beq; break;
        case 0x1: dec.branch = branch_op::Bne; break;
        case 0x4: dec.branch = branch_op::Blt; break;
        case 0x5: dec.branch = branch_op::Bge; break;
        case 0x6: dec.branch = branch_op::Bltu; break;
        case 0x7: dec.branch = branch_op::Bgeu; break;
        default: dec.illegal = true; break;
        }
        break;
    case 0x6f:
        // JAL/JALR 都写回 link address，并在 EXU commit 侧请求 IFU 重定向。
        dec.kind = inst_kind::Jal;
        dec.rd_write = true;
        dec.imm = imm_j(instr);
        break;
    case 0x67:
        dec.kind = inst_kind::Jalr;
        dec.rs1_read = true;
        dec.rd_write = true;
        dec.imm = imm_i(instr);
        dec.illegal = funct3 != 0x0;
        break;
    case 0x37:
        dec.kind = inst_kind::Lui;
        dec.rd_write = true;
        dec.imm = imm_u(instr);
        break;
    case 0x17:
        dec.kind = inst_kind::Auipc;
        dec.rd_write = true;
        dec.imm = imm_u(instr);
        break;
    case 0x0f:
        // FENCE/FENCE.I 在 v1 中作为串行化指令处理，要求 OITF empty 后提交。
        dec.kind = inst_kind::Fence;
        dec.illegal = funct3 != 0x0 && funct3 != 0x1;
        break;
    case 0x73:
        // SYSTEM：支持 machine trap/return 与 Zicsr 读改写。
        if (funct3 == 0x0) {
            if (dec.csr_addr == 0x000) dec.kind = inst_kind::Ecall;
            else if (dec.csr_addr == 0x001) dec.kind = inst_kind::Ebreak;
            else if (dec.csr_addr == 0x302) dec.kind = inst_kind::Mret;
            else dec.illegal = true;
        } else {
            dec.kind = inst_kind::Csr;
            dec.rd_write = true;
            dec.rs1_read = funct3 < 0x5;
            switch (funct3) {
            case 0x1: dec.csr = csr_op::Csrrw; break;
            case 0x2: dec.csr = csr_op::Csrrs; break;
            case 0x3: dec.csr = csr_op::Csrrc; break;
            case 0x5: dec.csr = csr_op::Csrrwi; break;
            case 0x6: dec.csr = csr_op::Csrrsi; break;
            case 0x7: dec.csr = csr_op::Csrrci; break;
            default: dec.illegal = true; break;
            }
        }
        break;
    default:
        dec.illegal = true;
        break;
    }

    if (dec.illegal) {
        // illegal 统一清除读写使能，避免后级在异常路径误做寄存器访问或 hazard。
        dec.kind = inst_kind::Illegal;
        dec.rd_write = false;
        dec.rs1_read = false;
        dec.rs2_read = false;
    }
    return dec;
}

} // namespace e203sim
