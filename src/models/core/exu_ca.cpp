#include "models/core/exu_ca.h"

#include "common/debug_logger.h"
#include "sim/difftest_manager.h"
#include "sim/pipe_trace.h"
#include "sim/thread_trace.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iomanip>
#include <sstream>

using namespace sc_core;

namespace {

// machine mode 同步异常 cause 编码，按 RISC-V privileged spec 的低位 cause 值保存。
constexpr uint32_t kCauseInstrAccessFault = 1;
constexpr uint32_t kCauseIllegalInst = 2;
constexpr uint32_t kCauseBreakpoint = 3;
constexpr uint32_t kCauseLoadAddrMisalign = 4;
constexpr uint32_t kCauseLoadAccessFault = 5;
constexpr uint32_t kCauseStoreAddrMisalign = 6;
constexpr uint32_t kCauseStoreAccessFault = 7;
constexpr uint32_t kCauseEcallM = 11;
constexpr uint32_t kMstatusMie = 1u << 3u;
constexpr uint32_t kMstatusMpie = 1u << 7u;
constexpr uint32_t kMstatusWritableMask = kMstatusMie | kMstatusMpie;

bool is_imm_alu(e203sim::inst_kind kind, uint32_t instr)
{
    // OP-IMM 与 OP 都归到 Alu 类别；这里用 opcode 判断 rhs 来源。
    return kind == e203sim::inst_kind::Alu && ((instr & 0x7fu) == 0x13u);
}

bool is_long_pipe(e203sim::inst_kind kind)
{
    return kind == e203sim::inst_kind::Load || kind == e203sim::inst_kind::Store;
}

bool needs_empty_oitf(e203sim::inst_kind kind)
{
    // CSR/fence/trap 类指令在 v1 中作为提交边界，等待 long-pipe 全部退休。
    return kind == e203sim::inst_kind::Csr ||
           kind == e203sim::inst_kind::Fence ||
           kind == e203sim::inst_kind::Ecall ||
           kind == e203sim::inst_kind::Ebreak ||
           kind == e203sim::inst_kind::Mret ||
           kind == e203sim::inst_kind::Illegal;
}

std::string hex32(uint32_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return oss.str();
}

std::string reg_name(uint8_t idx)
{
    return "x" + std::to_string(idx);
}

std::string signed_dec(uint32_t value)
{
    return std::to_string(static_cast<int32_t>(value));
}

std::string csr_name(uint16_t addr)
{
    switch (addr) {
    case 0x300: return "mstatus";
    case 0x304: return "mie";
    case 0x305: return "mtvec";
    case 0x340: return "mscratch";
    case 0x341: return "mepc";
    case 0x342: return "mcause";
    case 0x343: return "mtval";
    case 0x344: return "mip";
    case 0xb00: return "mcycle";
    case 0xb02: return "minstret";
    case 0xb80: return "mcycleh";
    case 0xb82: return "minstreth";
    case 0xbf0: return "mdvnob2b";
    case 0xbfd: return "itcmnohold";
    case 0xbfe: return "mcgstop";
    case 0xbff: return "counterstop";
    case 0xf11: return "mvendorid";
    case 0xf12: return "marchid";
    case 0xf13: return "mimpid";
    case 0xf14: return "mhartid";
    default: return hex32(addr);
    }
}

std::string alu_name(const e203sim::decoded_inst& dec, uint32_t instr)
{
    const bool imm = (instr & 0x7fu) == 0x13u;
    switch (dec.alu) {
    case e203sim::alu_op::Add: return imm ? "addi" : "add";
    case e203sim::alu_op::Sub: return "sub";
    case e203sim::alu_op::And: return imm ? "andi" : "and";
    case e203sim::alu_op::Or: return imm ? "ori" : "or";
    case e203sim::alu_op::Xor: return imm ? "xori" : "xor";
    case e203sim::alu_op::Sll: return imm ? "slli" : "sll";
    case e203sim::alu_op::Srl: return imm ? "srli" : "srl";
    case e203sim::alu_op::Sra: return imm ? "srai" : "sra";
    case e203sim::alu_op::Slt: return imm ? "slti" : "slt";
    case e203sim::alu_op::Sltu: return imm ? "sltiu" : "sltu";
    case e203sim::alu_op::PassB: return "pass";
    }
    return "alu";
}

std::string branch_name(e203sim::branch_op op)
{
    switch (op) {
    case e203sim::branch_op::Beq: return "beq";
    case e203sim::branch_op::Bne: return "bne";
    case e203sim::branch_op::Blt: return "blt";
    case e203sim::branch_op::Bge: return "bge";
    case e203sim::branch_op::Bltu: return "bltu";
    case e203sim::branch_op::Bgeu: return "bgeu";
    case e203sim::branch_op::None: return "branch";
    }
    return "branch";
}

std::string load_name(const e203sim::decoded_inst& dec)
{
    switch (dec.mem_size) {
    case e203sim::AccessSize::Byte: return dec.mem_unsigned ? "lbu" : "lb";
    case e203sim::AccessSize::HalfWord: return dec.mem_unsigned ? "lhu" : "lh";
    case e203sim::AccessSize::Word: return "lw";
    }
    return "load";
}

std::string store_name(const e203sim::decoded_inst& dec)
{
    switch (dec.mem_size) {
    case e203sim::AccessSize::Byte: return "sb";
    case e203sim::AccessSize::HalfWord: return "sh";
    case e203sim::AccessSize::Word: return "sw";
    }
    return "store";
}

std::string muldiv_name(e203sim::muldiv_op op)
{
    switch (op) {
    case e203sim::muldiv_op::Mul: return "mul";
    case e203sim::muldiv_op::Mulh: return "mulh";
    case e203sim::muldiv_op::Mulhsu: return "mulhsu";
    case e203sim::muldiv_op::Mulhu: return "mulhu";
    case e203sim::muldiv_op::Div: return "div";
    case e203sim::muldiv_op::Divu: return "divu";
    case e203sim::muldiv_op::Rem: return "rem";
    case e203sim::muldiv_op::Remu: return "remu";
    case e203sim::muldiv_op::None: return "muldiv";
    }
    return "muldiv";
}

std::string csr_op_name(e203sim::csr_op op)
{
    switch (op) {
    case e203sim::csr_op::Csrrw: return "csrrw";
    case e203sim::csr_op::Csrrs: return "csrrs";
    case e203sim::csr_op::Csrrc: return "csrrc";
    case e203sim::csr_op::Csrrwi: return "csrrwi";
    case e203sim::csr_op::Csrrsi: return "csrrsi";
    case e203sim::csr_op::Csrrci: return "csrrci";
    case e203sim::csr_op::None: return "csr";
    }
    return "csr";
}

std::string trace_label(const e203sim::decoded_inst& dec, uint32_t instr)
{
    const bool imm_alu = (instr & 0x7fu) == 0x13u;
    const uint32_t funct3 = (instr >> 12u) & 0x7u;
    switch (dec.kind) {
    case e203sim::inst_kind::Alu:
        return alu_name(dec, instr) + " " + reg_name(dec.rd) + "," + reg_name(dec.rs1) + "," +
               (imm_alu ? signed_dec(dec.imm) : reg_name(dec.rs2));
    case e203sim::inst_kind::Lui:
        return "lui " + reg_name(dec.rd) + "," + hex32(dec.imm);
    case e203sim::inst_kind::Auipc:
        return "auipc " + reg_name(dec.rd) + "," + hex32(dec.imm);
    case e203sim::inst_kind::MulDiv:
        return muldiv_name(dec.muldiv) + " " + reg_name(dec.rd) + "," + reg_name(dec.rs1) + "," +
               reg_name(dec.rs2);
    case e203sim::inst_kind::Branch:
        return branch_name(dec.branch) + " " + reg_name(dec.rs1) + "," + reg_name(dec.rs2) + "," +
               signed_dec(dec.imm);
    case e203sim::inst_kind::Jal:
        return "jal " + reg_name(dec.rd) + "," + signed_dec(dec.imm);
    case e203sim::inst_kind::Jalr:
        return "jalr " + reg_name(dec.rd) + "," + signed_dec(dec.imm) + "(" + reg_name(dec.rs1) + ")";
    case e203sim::inst_kind::Load:
        return load_name(dec) + " " + reg_name(dec.rd) + "," + signed_dec(dec.imm) + "(" +
               reg_name(dec.rs1) + ")";
    case e203sim::inst_kind::Store:
        return store_name(dec) + " " + reg_name(dec.rs2) + "," + signed_dec(dec.imm) + "(" +
               reg_name(dec.rs1) + ")";
    case e203sim::inst_kind::Csr:
        return csr_op_name(dec.csr) + " " + reg_name(dec.rd) + "," + csr_name(dec.csr_addr) + "," +
               (dec.rs1_read ? reg_name(dec.rs1) : std::to_string(dec.rs1));
    case e203sim::inst_kind::Fence:
        return funct3 == 0x1u ? "fence.i" : "fence";
    case e203sim::inst_kind::Ecall: return "ecall";
    case e203sim::inst_kind::Ebreak: return "ebreak";
    case e203sim::inst_kind::Mret: return "mret";
    case e203sim::inst_kind::Illegal: return "illegal " + hex32(instr);
    }
    return hex32(instr);
}

bool is_mul_op(e203sim::muldiv_op op)
{
    return op == e203sim::muldiv_op::Mul ||
           op == e203sim::muldiv_op::Mulh ||
           op == e203sim::muldiv_op::Mulhsu ||
           op == e203sim::muldiv_op::Mulhu;
}

bool is_div_op(e203sim::muldiv_op op)
{
    return op == e203sim::muldiv_op::Div ||
           op == e203sim::muldiv_op::Divu ||
           op == e203sim::muldiv_op::Rem ||
           op == e203sim::muldiv_op::Remu;
}

bool is_signed_div_op(e203sim::muldiv_op op)
{
    return op == e203sim::muldiv_op::Div || op == e203sim::muldiv_op::Rem;
}

uint64_t mul_u64(uint32_t lhs, uint32_t rhs)
{
    return static_cast<uint64_t>(lhs) * static_cast<uint64_t>(rhs);
}

uint64_t mul_s64(int32_t lhs, int32_t rhs)
{
    return static_cast<uint64_t>(static_cast<int64_t>(lhs) * static_cast<int64_t>(rhs));
}

uint64_t mul_su64(int32_t lhs, uint32_t rhs)
{
    const __int128 product = static_cast<__int128>(lhs) * static_cast<__int128>(rhs);
    return static_cast<uint64_t>(product);
}

uint8_t access_size_bytes(e203sim::AccessSize size)
{
    return static_cast<uint8_t>(size);
}

uint32_t store_data_masked(uint32_t data, e203sim::AccessSize size)
{
    switch (size) {
    case e203sim::AccessSize::Byte:
        return data & 0xffu;
    case e203sim::AccessSize::HalfWord:
        return data & 0xffffu;
    case e203sim::AccessSize::Word:
        return data;
    }
    return data;
}

bool csr_needs_ref_skip(uint16_t addr)
{
    switch (addr) {
    case 0x301:
    case 0xb00:
    case 0xb02:
    case 0xb80:
    case 0xb82:
    case 0xbf0:
    case 0xbfd:
    case 0xbfe:
    case 0xbff:
    case 0xf11:
    case 0xf12:
    case 0xf13:
    case 0xf14:
        return true;
    default:
        return false;
    }
}

} // namespace

exu_ca::exu_ca(sc_module_name module_name, const e203sim::sim_config& config)
    : sc_module(module_name), clk_period_(config.cycle_ns, SC_NS)
{
    lsu_ = new lsu_ca("lsu_ca", config);
    if (config.itcm != nullptr) {
        itcm_config_ = *config.itcm;
    }
    if (config.dtcm != nullptr) {
        dtcm_config_ = *config.dtcm;
    }
    csr_.mtvec = 0;
    SC_THREAD(thread);
    INFO(module_name << " created !");
}

exu_ca::~exu_ca()
{
    delete lsu_;
}

void exu_ca::bind_pipeline(e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t>& pipe)
{
    ifu_pipeline_port.bind(pipe);
}

lsu_ca& exu_ca::lsu()
{
    return *lsu_;
}

const lsu_ca& exu_ca::lsu() const
{
    return *lsu_;
}

regfile& exu_ca::regs()
{
    return regs_;
}

const regfile& exu_ca::regs() const
{
    return regs_;
}

const exu_ca::csr_state& exu_ca::csr() const
{
    return csr_;
}

bool exu_ca::oitf_empty() const
{
    return oitf_.empty();
}

std::size_t exu_ca::oitf_size() const
{
    return oitf_.size();
}

bool exu_ca::debug_reserve_oitf(uint8_t rd, bool rd_write, uint32_t pc)
{
    // 测试辅助入口不发 LSU 请求，只构造未完成 long-pipe 状态。
    if (oitf_.size() >= kOitfDepth) {
        return false;
    }
    oitf_.push_back(oitf_entry{next_itag_++, rd, rd_write, pc, e203sim::inst_kind::Load});
    return true;
}

bool exu_ca::has_oitf_hazard(const e203sim::decoded_inst& dec) const
{
    // OITF 只记录会在未来写回的 long-pipe rd；x0 写回不产生依赖。
    for (const auto& entry : oitf_) {
        if (entry.rd_write && entry.rd != 0) {
            if ((dec.rs1_read && dec.rs1 == entry.rd) ||
                (dec.rs2_read && dec.rs2 == entry.rd) ||
                (dec.rd_write && dec.rd == entry.rd)) {
                return true;
            }
        }
    }
    return false;
}

bool exu_ca::can_dispatch(const e203sim::decoded_inst& dec) const
{
    // dispatch stall 优先级：数据 hazard、串行化要求、long-pipe 结构资源。
    if (muldiv_.busy) {
        return false;
    }
    if (has_oitf_hazard(dec)) {
        return false;
    }
    if (needs_empty_oitf(dec.kind) && !oitf_.empty()) {
        return false;
    }
    if (is_long_pipe(dec.kind)) {
        return oitf_.size() < kOitfDepth && lsu_->request_ready();
    }
    return true;
}

void exu_ca::clear_muldiv_state()
{
    muldiv_ = muldiv_context{};
    muldiv_b2b_cache_ = muldiv_b2b_cache{};
}

bool exu_ca::div_special_case(e203sim::muldiv_op op, uint32_t rs1, uint32_t rs2) const
{
    if (!is_div_op(op)) {
        return false;
    }
    const bool div_by_zero = rs2 == 0;
    const bool overflow = is_signed_div_op(op) &&
                          rs1 == 0x80000000u &&
                          rs2 == 0xffffffffu;
    return div_by_zero || overflow;
}

bool exu_ca::div_needs_correction(e203sim::muldiv_op op, uint32_t rs1, uint32_t rs2) const
{
    if (!is_div_op(op) || div_special_case(op, rs1, rs2)) {
        return false;
    }

    // RTL 的 non-restoring divider 在部分非整除场景进入 quotient/remainder
    // correction 状态。CA 模型不复现逐拍余数寄存器，用架构余数非零划分额外修正延迟。
    if (op == e203sim::muldiv_op::Divu || op == e203sim::muldiv_op::Remu) {
        return (rs1 % rs2) != 0;
    }

    const int64_t lhs = static_cast<int32_t>(rs1);
    const int64_t rhs = static_cast<int32_t>(rs2);
    return (lhs % rhs) != 0;
}

uint32_t exu_ca::muldiv_latency(e203sim::muldiv_op op,
                                uint32_t rs1,
                                uint32_t rs2,
                                bool b2b_hit) const
{
    if (b2b_hit) {
        return 1;
    }
    if (is_mul_op(op)) {
        return 17;
    }
    if (div_special_case(op, rs1, rs2)) {
        return 1;
    }
    return div_needs_correction(op, rs1, rs2) ? 35 : 33;
}

uint32_t exu_ca::execute_muldiv(e203sim::muldiv_op op, uint32_t rs1, uint32_t rs2) const
{
    switch (op) {
    case e203sim::muldiv_op::Mul:
        return static_cast<uint32_t>(mul_s64(static_cast<int32_t>(rs1), static_cast<int32_t>(rs2)));
    case e203sim::muldiv_op::Mulh:
        return static_cast<uint32_t>(mul_s64(static_cast<int32_t>(rs1), static_cast<int32_t>(rs2)) >> 32u);
    case e203sim::muldiv_op::Mulhsu:
        return static_cast<uint32_t>(mul_su64(static_cast<int32_t>(rs1), rs2) >> 32u);
    case e203sim::muldiv_op::Mulhu:
        return static_cast<uint32_t>(mul_u64(rs1, rs2) >> 32u);
    case e203sim::muldiv_op::Div:
        if (rs2 == 0) return 0xffffffffu;
        if (rs1 == 0x80000000u && rs2 == 0xffffffffu) return 0x80000000u;
        return static_cast<uint32_t>(static_cast<int32_t>(rs1) / static_cast<int32_t>(rs2));
    case e203sim::muldiv_op::Divu:
        return rs2 == 0 ? 0xffffffffu : rs1 / rs2;
    case e203sim::muldiv_op::Rem:
        if (rs2 == 0) return rs1;
        if (rs1 == 0x80000000u && rs2 == 0xffffffffu) return 0;
        return static_cast<uint32_t>(static_cast<int32_t>(rs1) % static_cast<int32_t>(rs2));
    case e203sim::muldiv_op::Remu:
        return rs2 == 0 ? rs1 : rs1 % rs2;
    case e203sim::muldiv_op::None:
        return 0;
    }
    return 0;
}

bool exu_ca::muldiv_b2b_hit(e203sim::muldiv_op op, uint32_t rs1, uint32_t rs2, bool packet_b2b) const
{
    if (!packet_b2b || !muldiv_b2b_cache_.valid) {
        return false;
    }
    const bool pair_match =
        (op == e203sim::muldiv_op::Mul &&
         (muldiv_b2b_cache_.op == e203sim::muldiv_op::Mulh ||
          muldiv_b2b_cache_.op == e203sim::muldiv_op::Mulhsu ||
          muldiv_b2b_cache_.op == e203sim::muldiv_op::Mulhu)) ||
        (op == e203sim::muldiv_op::Div && muldiv_b2b_cache_.op == e203sim::muldiv_op::Rem) ||
        (op == e203sim::muldiv_op::Rem && muldiv_b2b_cache_.op == e203sim::muldiv_op::Div) ||
        (op == e203sim::muldiv_op::Divu && muldiv_b2b_cache_.op == e203sim::muldiv_op::Remu) ||
        (op == e203sim::muldiv_op::Remu && muldiv_b2b_cache_.op == e203sim::muldiv_op::Divu);
    return pair_match && rs1 == muldiv_b2b_cache_.rs1 && rs2 == muldiv_b2b_cache_.rs2;
}

void exu_ca::update_muldiv_b2b_cache(e203sim::muldiv_op op, uint32_t rs1, uint32_t rs2)
{
    muldiv_b2b_cache_.valid = true;
    muldiv_b2b_cache_.op = op;
    muldiv_b2b_cache_.rs1 = rs1;
    muldiv_b2b_cache_.rs2 = rs2;
    muldiv_b2b_cache_.product_low =
        static_cast<uint32_t>(mul_s64(static_cast<int32_t>(rs1), static_cast<int32_t>(rs2)));
    if (is_div_op(op) && !div_special_case(op, rs1, rs2)) {
        if (op == e203sim::muldiv_op::Divu || op == e203sim::muldiv_op::Remu) {
            muldiv_b2b_cache_.quotient = rs1 / rs2;
            muldiv_b2b_cache_.remainder = rs1 % rs2;
        } else {
            const int32_t lhs = static_cast<int32_t>(rs1);
            const int32_t rhs = static_cast<int32_t>(rs2);
            muldiv_b2b_cache_.quotient = static_cast<uint32_t>(lhs / rhs);
            muldiv_b2b_cache_.remainder = static_cast<uint32_t>(lhs % rhs);
        }
    } else {
        muldiv_b2b_cache_.quotient = execute_muldiv(e203sim::muldiv_op::Div, rs1, rs2);
        muldiv_b2b_cache_.remainder = execute_muldiv(e203sim::muldiv_op::Rem, rs1, rs2);
    }
}

void exu_ca::start_muldiv(const e203sim::decoded_inst& dec,
                          const e203sim::ifu_exu_packet& pkt,
                          uint32_t rs1,
                          uint32_t rs2)
{
    const bool b2b_hit = muldiv_b2b_hit(dec.muldiv, rs1, rs2, pkt.muldiv_b2b);
    uint32_t result = execute_muldiv(dec.muldiv, rs1, rs2);
    if (b2b_hit) {
        if (dec.muldiv == e203sim::muldiv_op::Mul) {
            result = muldiv_b2b_cache_.product_low;
        } else if (dec.muldiv == e203sim::muldiv_op::Div || dec.muldiv == e203sim::muldiv_op::Divu) {
            result = muldiv_b2b_cache_.quotient;
        } else if (dec.muldiv == e203sim::muldiv_op::Rem || dec.muldiv == e203sim::muldiv_op::Remu) {
            result = muldiv_b2b_cache_.remainder;
        }
    }

    muldiv_.busy = true;
    muldiv_.op = dec.muldiv;
    muldiv_.rd = dec.rd;
    muldiv_.rd_write = dec.rd_write;
    muldiv_.result = result;
    muldiv_.remaining_cycles = muldiv_latency(dec.muldiv, rs1, rs2, b2b_hit);
    muldiv_.pc = pkt.pc;
    muldiv_.instr = pkt.instr;
    muldiv_.trace_seq = pkt.trace_seq;
    muldiv_.trace_fetch_tick = pkt.trace_fetch_tick;
    muldiv_.trace_label = trace_label(dec, pkt.instr);
    update_muldiv_b2b_cache(dec.muldiv, rs1, rs2);

    THREAD_TRACE("EXU执行线程", b2b_hit ? "启动MULDIV融合" : "启动MULDIV",
                 "PC=0x" << std::hex << pkt.pc
                 << " 指令=0x" << pkt.instr << std::dec
                 << " 延迟=" << muldiv_.remaining_cycles);
}

bool exu_ca::step_muldiv()
{
    if (!muldiv_.busy) {
        return false;
    }
    SIM_ASSERT(muldiv_.remaining_cycles > 0, "MULDIV busy with zero remaining cycles");
    --muldiv_.remaining_cycles;
    THREAD_TRACE("EXU执行线程", "推进MULDIV", "剩余=" << muldiv_.remaining_cycles);
    if (muldiv_.remaining_cycles != 0) {
        return true;
    }

    regs_.write(muldiv_.rd, muldiv_.result, muldiv_.rd_write);
    commit_result result;
    result.valid = true;
    result.pc = muldiv_.pc;
    result.instr = muldiv_.instr;
    result.trace_seq = muldiv_.trace_seq;
    result.trace_fetch_tick = muldiv_.trace_fetch_tick;
    result.trace_label = muldiv_.trace_label;
    result.trace_valid = e203sim::pipe_trace::instance().enabled();
    result.next_pc = muldiv_.pc + 4u;
    commit(result);
    THREAD_TRACE("EXU执行线程", "完成MULDIV",
                 "PC=0x" << std::hex << muldiv_.pc
                 << " 结果=0x" << muldiv_.result << std::dec);
    muldiv_ = muldiv_context{};
    return true;
}

uint32_t exu_ca::read_csr(uint16_t addr) const
{
    // 对齐 e203 RTL：未知 CSR 读 0，写忽略，不在 CSR 单元产生 illegal。
    switch (addr) {
    case 0x300: return (csr_.mstatus & kMstatusWritableMask) | (0x3u << 11u);
    case 0x301: return 0x40001104u; // RV32 M/I/C，贴近当前 e203 配置。
    case 0x304: return csr_.mie & 0x888u;
    case 0x305: return csr_.mtvec;
    case 0x340: return csr_.mscratch;
    case 0x341: return csr_.mepc;
    case 0x342: return csr_.mcause;
    case 0x343: return csr_.mtval;
    case 0x344: return csr_.mip & 0x888u;
    case 0xb00: return static_cast<uint32_t>(csr_.mcycle);
    case 0xb02: return static_cast<uint32_t>(csr_.minstret);
    case 0xb80: return static_cast<uint32_t>(csr_.mcycle >> 32u);
    case 0xb82: return static_cast<uint32_t>(csr_.minstret >> 32u);
    case 0xbf0: return csr_.mdvnob2b & 0x1u;
    case 0xbfd: return csr_.itcmnohold & 0x1u;
    case 0xbfe: return csr_.mcgstop & 0x3u;
    case 0xbff: return csr_.counterstop & 0x7u;
    case 0xf11: return 0x536u;
    case 0xf12: return 0xe203u;
    case 0xf13: return 0x1u;
    case 0xf14: return 0x0u;
    default: return 0;
    }
}

void exu_ca::write_csr(uint16_t addr, uint32_t value)
{
    switch (addr) {
    case 0x300: csr_.mstatus = value & kMstatusWritableMask; break;
    case 0x304: csr_.mie = value & 0x888u; break;
    case 0x305: csr_.mtvec = value; break;
    case 0x340: csr_.mscratch = value; break;
    case 0x341: csr_.mepc = value & ~0x1u; break;
    case 0x342: csr_.mcause = value & 0x8000000fu; break;
    case 0x343: csr_.mtval = value; break;
    case 0xb00: csr_.mcycle = (csr_.mcycle & 0xffffffff00000000ull) | value; break;
    case 0xb80: csr_.mcycle = (csr_.mcycle & 0x00000000ffffffffull) |
                              (static_cast<uint64_t>(value) << 32u); break;
    case 0xb02: csr_.minstret = (csr_.minstret & 0xffffffff00000000ull) | value; break;
    case 0xb82: csr_.minstret = (csr_.minstret & 0x00000000ffffffffull) |
                                (static_cast<uint64_t>(value) << 32u); break;
    case 0xbf0: csr_.mdvnob2b = value & 0x1u; break;
    case 0xbfd: csr_.itcmnohold = value & 0x1u; break;
    case 0xbfe: csr_.mcgstop = value & 0x3u; break;
    case 0xbff: csr_.counterstop = value & 0x7u; break;
    default: break;
    }
}

void exu_ca::enter_trap(uint32_t pc, uint32_t cause, uint32_t value)
{
    const bool mie = (csr_.mstatus & kMstatusMie) != 0;
    csr_.mstatus &= ~(kMstatusMie | kMstatusMpie);
    if (mie) {
        csr_.mstatus |= kMstatusMpie;
    }
    csr_.mepc = pc & ~0x1u;
    csr_.mcause = cause & 0x8000000fu;
    csr_.mtval = value;
}

void exu_ca::retire_mret()
{
    const bool mpie = (csr_.mstatus & kMstatusMpie) != 0;
    csr_.mstatus &= ~kMstatusMie;
    if (mpie) {
        csr_.mstatus |= kMstatusMie;
    }
    csr_.mstatus |= kMstatusMpie;
}

bool exu_ca::in_tcm(uint32_t addr) const
{
    const auto in_region = [](const e203sim::memory_config& cfg, uint32_t value) {
        return cfg.size != 0 && value >= cfg.base_addr && value < cfg.base_addr + cfg.size;
    };
    return in_region(itcm_config_, addr) || in_region(dtcm_config_, addr);
}

void exu_ca::difftest_commit(const commit_result& result, uint32_t next_pc)
{
    if (!e203sim::difftest_manager::instance().enabled()) {
        return;
    }

    e203sim::diff_retire_event event;
    event.pc = result.pc;
    event.next_pc = next_pc;
    event.instr = result.instr;
    event.regs.gpr = regs_.snapshot();
    event.regs.pc = next_pc;
    event.has_store = result.has_store;
    event.store = result.store;
    event.skip_ref = result.skip_difftest_ref;
    event.skip_reason = result.skip_difftest_reason;
    e203sim::difftest_manager::instance().step(event);
}

void exu_ca::commit(const commit_result& result)
{
    if (!result.valid) {
        return;
    }

    if (result.trace_valid) {
        auto& trace = e203sim::pipe_trace::instance();
        const uint64_t now = trace.now_tick();
        const uint64_t cycle = trace.cycle_tick();
        const uint64_t decode = std::min(result.trace_fetch_tick + cycle, now);
        trace.emit_instruction(result.trace_seq,
                               result.pc,
                               result.instr,
                               result.trace_fetch_tick,
                               decode,
                               now,
                               now,
                               now,
                               now,
                               result.trace_label.empty() ? hex32(result.instr) : result.trace_label);
    }

    if (result.trap) {
        // trap 不增加 minstret；更新 trap CSR 后请求 IFU 跳转到 mtvec。
        SIM_ARCH_EXCEPTION(basename(), "trap pc=0x" << std::hex << result.pc
                                      << ", cause=0x" << result.trap_cause
                                      << ", mtval=0x" << result.trap_value
                                      << ", mtvec=0x" << csr_.mtvec << std::dec);
        enter_trap(result.pc, result.trap_cause, result.trap_value);
        flush_pending_ = true;
        flush_pc_ = csr_.mtvec;
        difftest_commit(result, csr_.mtvec);
        if (result.trace_valid) {
            e203sim::pipe_trace::instance().emit_detail(e203sim::pipe_trace::instance().now_tick(),
                                                        "system.cpu.exu",
                                                        result.trace_seq,
                                                        "trap pc=" + hex32(result.pc) +
                                                            " cause=" + hex32(result.trap_cause) +
                                                            " mtval=" + hex32(result.trap_value));
        }
        return;
    }

    if ((csr_.counterstop & 0x4u) == 0) {
        ++csr_.minstret;
    }
    if (result.flush) {
        // 非异常 flush 用于 branch/jump resolve 或预测纠正。
        flush_pending_ = true;
        flush_pc_ = result.flush_pc;
    }
    const uint32_t next_pc = result.next_pc != 0 ? result.next_pc :
                             (result.flush ? result.flush_pc : result.pc + 4u);
    difftest_commit(result, next_pc);
}

void exu_ca::handle_lsu_response(const lsu_ca::response& rsp)
{
    SIM_ASSERT(!oitf_.empty(), "LSU response without OITF entry");
    // itag/order mismatch 是 EXU/LSU/OITF 建模错误，不是被模拟软件的架构异常。
    SIM_ASSERT(oitf_.front().itag == rsp.itag, "LSU response itag does not match OITF oldest entry");

    oitf_.pop_front();

    // long-pipe writeback 优先于 ALU dispatch；错误响应不写寄存器。
    commit_result result;
    result.valid = true;
    result.pc = rsp.pc;
    result.instr = rsp.instr;
    result.trace_seq = rsp.trace_seq;
    result.trace_fetch_tick = rsp.trace_fetch_tick;
    result.trace_label = rsp.trace_label;
    result.trace_valid = e203sim::pipe_trace::instance().enabled();
    result.next_pc = rsp.pc + 4u;
    if (!in_tcm(rsp.addr)) {
        result.skip_difftest_ref = true;
        result.skip_difftest_reason = "mmio";
    }
    if (!rsp.load && !rsp.error) {
        result.has_store = true;
        result.store.vaddr = rsp.addr;
        result.store.data = store_data_masked(rsp.store_data, rsp.size);
        result.store.len = access_size_bytes(rsp.size);
        result.store.type = 2;
    }
    if (rsp.error) {
        result.trap = true;
        result.trap_cause = rsp.load ? kCauseLoadAccessFault : kCauseStoreAccessFault;
        result.trap_value = rsp.badaddr;
    } else if (rsp.rd_write) {
        regs_.write(rsp.rd, rsp.load_data, true);
    }
    commit(result);
}

void exu_ca::dispatch_packet(const e203sim::ifu_exu_packet& pkt)
{
    commit_result result;
    result.valid = true;
    result.pc = pkt.pc;
    result.instr = pkt.instr;
    result.trace_seq = pkt.trace_seq;
    result.trace_fetch_tick = pkt.trace_fetch_tick;
    result.trace_valid = e203sim::pipe_trace::instance().enabled();
    result.next_pc = pkt.pc + (pkt.instr_32bit ? 4u : 2u);

    if (pkt.bus_error || pkt.misalign || !pkt.pc_valid) {
        // IFU 同步异常在 EXU commit 侧架构化，mtval 使用 faulting PC。
        result.trap = true;
        result.trap_cause = pkt.misalign ? kCauseInstrAccessFault : kCauseInstrAccessFault;
        result.trap_value = pkt.pc;
        result.skip_difftest_ref = true;
        result.skip_difftest_reason = "fetch exception";
        commit(result);
        return;
    }

    const e203sim::decoded_inst dec = e203sim::decode_rv32i(pkt.instr, pkt.instr_32bit);
    result.trace_label = trace_label(dec, pkt.instr);
    if (dec.illegal) {
        // illegal instruction trap 的 mtval 保存原始指令编码。
        result.trap = true;
        result.trap_cause = kCauseIllegalInst;
        result.trap_value = pkt.instr;
        commit(result);
        return;
    }

    const uint32_t rs1 = regs_.read(dec.rs1);
    const uint32_t rs2 = regs_.read(dec.rs2);

    switch (dec.kind) {
    case e203sim::inst_kind::Alu: {
        // 普通 ALU 指令在 EXU 本 cycle 产生写回和提交结果。
        const uint32_t rhs = is_imm_alu(dec.kind, pkt.instr) ? dec.imm : rs2;
        regs_.write(dec.rd, alu_.execute(dec.alu, rs1, rhs), dec.rd_write);
        commit(result);
        break;
    }
    case e203sim::inst_kind::MulDiv:
        // e203 shared MULDIV 不进入 OITF，而是在 ALU/EXU 内阻塞到结果提交。
        start_muldiv(dec, pkt, rs1, rs2);
        break;
    case e203sim::inst_kind::Lui:
        regs_.write(dec.rd, dec.imm, dec.rd_write);
        commit(result);
        break;
    case e203sim::inst_kind::Auipc:
        regs_.write(dec.rd, pkt.pc + dec.imm, dec.rd_write);
        commit(result);
        break;
    case e203sim::inst_kind::Branch: {
        // branch 目标由 EXU 计算；若与 IFU prediction 不一致则 flush。
        const bool taken = alu_.branch_taken(dec.branch, rs1, rs2);
        const uint32_t target = taken ? pkt.pc + dec.imm : pkt.pc + 4u;
        result.flush = taken != pkt.prdt_taken;
        result.flush_pc = target;
        result.next_pc = target;
        commit(result);
        break;
    }
    case e203sim::inst_kind::Jal:
        regs_.write(dec.rd, pkt.pc + 4u, dec.rd_write);
        result.flush = true;
        result.flush_pc = pkt.pc + dec.imm;
        result.next_pc = result.flush_pc;
        commit(result);
        break;
    case e203sim::inst_kind::Jalr:
        regs_.write(dec.rd, pkt.pc + 4u, dec.rd_write);
        result.flush = true;
        result.flush_pc = (rs1 + dec.imm) & ~1u;
        result.next_pc = result.flush_pc;
        commit(result);
        break;
    case e203sim::inst_kind::Load:
    case e203sim::inst_kind::Store: {
        // AGU 地址生成：base(rs1) + sign-extended immediate。
        const uint32_t addr = rs1 + dec.imm;
        const bool misalign = (dec.mem_size == e203sim::AccessSize::HalfWord && (addr & 0x1u) != 0) ||
                              (dec.mem_size == e203sim::AccessSize::Word && (addr & 0x3u) != 0);
        if (misalign) {
            result.trap = true;
            result.trap_cause = dec.kind == e203sim::inst_kind::Load ? kCauseLoadAddrMisalign
                                                                      : kCauseStoreAddrMisalign;
            result.trap_value = addr;
            commit(result);
            break;
        }

        const uint32_t itag = next_itag_++;
        // load/store 作为 long-pipe 进入 OITF，等待 LSU response 后提交。
        oitf_.push_back(oitf_entry{itag, dec.rd, dec.rd_write, pkt.pc, dec.kind});
        lsu_ca::request req;
        req.itag = itag;
        req.pc = pkt.pc;
        req.addr = addr;
        req.store_data = rs2;
        req.rd = dec.rd;
        req.instr = pkt.instr;
        req.trace_seq = pkt.trace_seq;
        req.trace_fetch_tick = pkt.trace_fetch_tick;
        req.trace_label = result.trace_label;
        req.size = dec.mem_size;
        req.load = dec.kind == e203sim::inst_kind::Load;
        req.unsigned_load = dec.mem_unsigned;
        req.rd_write = dec.rd_write;
        const bool issued = lsu_->issue(req);
        SIM_ASSERT(issued, "EXU dispatch checked LSU ready but issue failed");
        break;
    }
    case e203sim::inst_kind::Csr: {
        // CSR 指令返回 old CSR value；写入值按 csrrw/csrrs/csrrc 规则生成。
        const uint32_t old = read_csr(dec.csr_addr);
        const uint32_t src = dec.rs1_read ? rs1 : dec.rs1;
        uint32_t next = old;
        switch (dec.csr) {
        case e203sim::csr_op::Csrrw:
        case e203sim::csr_op::Csrrwi:
            next = src;
            break;
        case e203sim::csr_op::Csrrs:
        case e203sim::csr_op::Csrrsi:
            next = old | src;
            break;
        case e203sim::csr_op::Csrrc:
        case e203sim::csr_op::Csrrci:
            next = old & ~src;
            break;
        case e203sim::csr_op::None:
            break;
        }
        write_csr(dec.csr_addr, next);
        regs_.write(dec.rd, old, dec.rd_write);
        result.skip_difftest_ref = true;
        result.skip_difftest_reason = csr_needs_ref_skip(dec.csr_addr) ? "implementation csr" : "csr";
        commit(result);
        break;
    }
    case e203sim::inst_kind::Fence:
        if (((pkt.instr >> 12u) & 0x7u) == 0x1u) {
            result.flush = true;
            result.flush_pc = pkt.pc + (pkt.instr_32bit ? 4u : 2u);
            result.next_pc = result.flush_pc;
        }
        commit(result);
        break;
    case e203sim::inst_kind::Ecall:
        result.trap = true;
        result.trap_cause = kCauseEcallM;
        result.trap_value = 0;
        result.skip_difftest_ref = true;
        result.skip_difftest_reason = "ecall";
        commit(result);
        break;
    case e203sim::inst_kind::Mret:
        retire_mret();
        result.flush = true;
        result.flush_pc = csr_.mepc;
        result.next_pc = result.flush_pc;
        result.skip_difftest_ref = true;
        result.skip_difftest_reason = "mret";
        commit(result);
        break;
    case e203sim::inst_kind::Ebreak:
        // 本模拟器约定 ebreak 为仿真结束指令，不进入架构 trap。
        if (result.trace_valid) {
            auto& trace = e203sim::pipe_trace::instance();
            const uint64_t now = trace.now_tick();
            const uint64_t decode = std::min(result.trace_fetch_tick + trace.cycle_tick(), now);
            trace.emit_instruction(result.trace_seq,
                                   result.pc,
                                   result.instr,
                                   result.trace_fetch_tick,
                                   decode,
                                   now,
                                   now,
                                   now,
                                   now,
                                   trace_label(dec, pkt.instr));
            trace.emit_detail(now,
                              "system.cpu.exu",
                              result.trace_seq,
                              "ebreak stops simulation pc=" + hex32(pkt.pc));
        }
        SIM_ARCH_EXCEPTION(basename(), "ebreak stops simulation pc=0x"
                                      << std::hex << pkt.pc << std::dec);
        result.next_pc = pkt.pc + 4u;
        result.skip_difftest_ref = true;
        result.skip_difftest_reason = "ebreak";
        difftest_commit(result, result.next_pc);
        sc_core::sc_stop();
        break;
    case e203sim::inst_kind::Illegal:
        break;
    }
}

void exu_ca::thread()
{
    // 初始不接收 IFU packet，等待 SystemC 信号稳定。
    ifu_pipeline_port->set_ready(false);
    wait(SC_ZERO_TIME);

    while (true) {
        wait(clk_period_);
        // 模拟 RTL 中 long-pipe response -> EXU commit/dispatch 控制的同周期可见路径。
        // DTCM/LSU response 线程与 EXU 同时唤醒时，先让一个 delta cycle，使返回包进入 LSU response queue。
        wait(SC_ZERO_TIME);
        if ((csr_.counterstop & 0x1u) == 0) {
            ++csr_.mcycle;
        }
        THREAD_TRACE("EXU执行线程", "线程唤醒", "mcycle=" << csr_.mcycle
                     << " 待冲刷=" << flush_pending_
                     << " 输入有效=" << ifu_pipeline_port->valid()
                     << " OITF占用=" << oitf_.size()
                     << " MULDIV忙=" << muldiv_.busy);

        if (flush_pending_) {
            // flush 优先级最高：向 IFU 发送新 epoch 和目标 PC，并暂停本周期 dispatch。
            ifu_pipeline_port->set_ready(false);
            ifu_pipeline_port->pulse_flush(flush_pc_);
            THREAD_TRACE("EXU执行线程", "发出冲刷",
                         "目标PC=0x" << std::hex << flush_pc_ << std::dec
                                      << " flush_epoch=" << ifu_pipeline_port->flush_epoch());
            flush_pending_ = false;
            waiting_packet_clear_ = false;
            clear_muldiv_state();
            continue;
        }

        bool long_wb_this_cycle = false;
        if (lsu_->response_valid()) {
            // 写回仲裁：long-pipe response 先于新 ALU 指令 dispatch。
            // 这对应 E203 commit/long-pipe 返回优先级：先释放 OITF/写回/提交，
            // 同周期不再消费新的 IFU packet，避免年轻指令越过 long-pipe 提交点。
            THREAD_TRACE("EXU执行线程", "处理访存返回", "接收LSU结果");
            handle_lsu_response(lsu_->take_response());
            long_wb_this_cycle = true;
        }

        if (long_wb_this_cycle || flush_pending_) {
            // 本周期被 long-pipe 返回或新产生的 flush 占用，EXU 不接收 IFU packet。
            // IFU 会看到 ready=false 并保持队首 packet，下一周期再继续派发。
            ifu_pipeline_port->set_ready(false);
            continue;
        }

        if (step_muldiv() || flush_pending_) {
            // MULDIV 是 EXU 内部阻塞阶段：执行期间不接收新的 IFU packet。
            ifu_pipeline_port->set_ready(false);
            continue;
        }

        if (!ifu_pipeline_port->valid()) {
            THREAD_TRACE("EXU执行线程", "空闲等待", "IFU输入无效");
            ifu_pipeline_port->set_ready(true);
            waiting_packet_clear_ = false;
            continue;
        }

        const auto pkt = ifu_pipeline_port->read();
        if (waiting_packet_clear_ && pkt == last_consumed_packet_) {
            ifu_pipeline_port->set_ready(true);
            continue;
        }

        const auto dec = e203sim::decode_rv32i(pkt.instr, pkt.instr_32bit);
        const bool ready = can_dispatch(dec);
        THREAD_TRACE("EXU执行线程", ready ? "派发执行" : "暂停派发",
                     "PC=0x" << std::hex << pkt.pc
                     << " 指令=0x" << pkt.instr << std::dec
                     << " 序号=" << pkt.trace_seq);
        // ready=false 时 IFU 必须保持当前 packet，直到 hazard/resource 解除。
        // ready=true 时，EXU 在本周期消费 packet；IFU 下一轮看到 ready 后弹出队首。
        ifu_pipeline_port->set_ready(ready);
        if (ready) {
            dispatch_packet(pkt);
            waiting_packet_clear_ = true;
            last_consumed_packet_ = pkt;
            if (flush_pending_) {
                // RTL commit 侧同周期组合产生 pipe_flush_req；CA 模型在 dispatch 后立即递增
                // flush epoch，由 IFU 的 flush event 在同一仿真时间响应且只处理一次。
                ifu_pipeline_port->pulse_flush(flush_pc_);
                THREAD_TRACE("EXU执行线程", "发出冲刷",
                             "目标PC=0x" << std::hex << flush_pc_ << std::dec
                                          << " flush_epoch=" << ifu_pipeline_port->flush_epoch());
                flush_pending_ = false;
            }
        }   
    }
}
