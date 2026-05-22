#include "common/debug_logger.h"
#include "common/pipeline_if.h"
#include "common/types.h"
#include "isa/rv32i_decode.h"
#include "models/core/alu_unit.h"
#include "models/core/exu_ca.h"
#include "models/core/regfile.h"
#include "models/memory/dtcm_ca.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <tlm_utils/simple_target_socket.h>

namespace {

// 测试使用一个最小 DTCM 窗口和 10 ns cycle，直接驱动 IFU->EXU pipeline。
constexpr uint32_t kDtcmBase = 0x90000000u;
constexpr uint32_t kDtcmSize = 0x10000u;
constexpr uint32_t kCycleNs = 10u;

[[noreturn]] void fail(const std::string& msg)
{
    std::cerr << "[TEST][FAIL] " << sc_core::sc_time_stamp() << " " << msg << std::endl;
    std::exit(1);
}

void expect(bool cond, const std::string& msg)
{
    if (!cond) {
        fail(msg);
    }
}

uint32_t encode_addi(uint32_t rd, uint32_t rs1, int32_t imm)
{
    // 以下 encode_* helper 只生成本测试覆盖到的 RV32I 指令格式。
    return ((static_cast<uint32_t>(imm) & 0xfffu) << 20u) |
           ((rs1 & 0x1fu) << 15u) |
           (0x0u << 12u) |
           ((rd & 0x1fu) << 7u) |
           0x13u;
}

uint32_t encode_alu(uint32_t rd, uint32_t rs1, uint32_t rs2, uint32_t funct3, uint32_t funct7)
{
    return (funct7 << 25u) |
           ((rs2 & 0x1fu) << 20u) |
           ((rs1 & 0x1fu) << 15u) |
           ((funct3 & 0x7u) << 12u) |
           ((rd & 0x1fu) << 7u) |
           0x33u;
}

uint32_t encode_muldiv(uint32_t rd, uint32_t rs1, uint32_t rs2, uint32_t funct3)
{
    return encode_alu(rd, rs1, rs2, funct3, 0x01u);
}

uint32_t encode_lw(uint32_t rd, uint32_t rs1, int32_t imm)
{
    return ((static_cast<uint32_t>(imm) & 0xfffu) << 20u) |
           ((rs1 & 0x1fu) << 15u) |
           (0x2u << 12u) |
           ((rd & 0x1fu) << 7u) |
           0x03u;
}

uint32_t encode_lhu(uint32_t rd, uint32_t rs1, int32_t imm)
{
    return ((static_cast<uint32_t>(imm) & 0xfffu) << 20u) |
           ((rs1 & 0x1fu) << 15u) |
           (0x5u << 12u) |
           ((rd & 0x1fu) << 7u) |
           0x03u;
}

uint32_t encode_sw(uint32_t rs2, uint32_t rs1, int32_t imm)
{
    const uint32_t uimm = static_cast<uint32_t>(imm) & 0xfffu;
    return (((uimm >> 5u) & 0x7fu) << 25u) |
           ((rs2 & 0x1fu) << 20u) |
           ((rs1 & 0x1fu) << 15u) |
           (0x2u << 12u) |
           ((uimm & 0x1fu) << 7u) |
           0x23u;
}

uint32_t encode_sh(uint32_t rs2, uint32_t rs1, int32_t imm)
{
    const uint32_t uimm = static_cast<uint32_t>(imm) & 0xfffu;
    return (((uimm >> 5u) & 0x7fu) << 25u) |
           ((rs2 & 0x1fu) << 20u) |
           ((rs1 & 0x1fu) << 15u) |
           (0x1u << 12u) |
           ((uimm & 0x1fu) << 7u) |
           0x23u;
}

uint32_t encode_beq(uint32_t rs1, uint32_t rs2, int32_t imm)
{
    const uint32_t uimm = static_cast<uint32_t>(imm) & 0x1fffu;
    return (((uimm >> 12u) & 0x1u) << 31u) |
           (((uimm >> 5u) & 0x3fu) << 25u) |
           ((rs2 & 0x1fu) << 20u) |
           ((rs1 & 0x1fu) << 15u) |
           (((uimm >> 1u) & 0xfu) << 8u) |
           (((uimm >> 11u) & 0x1u) << 7u) |
           0x63u;
}

uint32_t encode_jal(uint32_t rd, int32_t imm)
{
    const uint32_t uimm = static_cast<uint32_t>(imm) & 0x1fffffu;
    return (((uimm >> 20u) & 0x1u) << 31u) |
           (((uimm >> 1u) & 0x3ffu) << 21u) |
           (((uimm >> 11u) & 0x1u) << 20u) |
           (((uimm >> 12u) & 0xffu) << 12u) |
           ((rd & 0x1fu) << 7u) |
           0x6fu;
}

uint32_t encode_csrrw(uint32_t rd, uint32_t csr, uint32_t rs1)
{
    return ((csr & 0xfffu) << 20u) |
           ((rs1 & 0x1fu) << 15u) |
           (0x1u << 12u) |
           ((rd & 0x1fu) << 7u) |
           0x73u;
}

uint32_t encode_csr(uint32_t rd, uint32_t csr, uint32_t rs1_or_zimm, uint32_t funct3)
{
    return ((csr & 0xfffu) << 20u) |
           ((rs1_or_zimm & 0x1fu) << 15u) |
           ((funct3 & 0x7u) << 12u) |
           ((rd & 0x1fu) << 7u) |
           0x73u;
}

uint32_t encode_fence_i()
{
    return 0x0000100fu;
}

uint32_t encode_mret()
{
    return 0x30200073u;
}

e203sim::ifu_exu_packet packet(uint32_t pc, uint32_t instr, bool prdt_taken = false)
{
    // 构造一个 IFU 已成功取到 32-bit 指令的 packet。
    e203sim::ifu_exu_packet pkt;
    pkt.pc = pc;
    pkt.instr = instr;
    pkt.instr_32bit = true;
    pkt.pc_valid = true;
    pkt.prdt_taken = prdt_taken;
    return pkt;
}

e203sim::ifu_exu_packet b2b_packet(uint32_t pc, uint32_t instr)
{
    auto pkt = packet(pc, instr);
    pkt.muldiv_b2b = true;
    return pkt;
}

class error_target : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<error_target> socket;

    explicit error_target(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
    {
        socket.register_nb_transport_fw(this, &error_target::nb_transport_fw);
    }

    tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay)
    {
        if (phase == tlm::BEGIN_REQ) {
            // BIU/未连接外设占位：任何访问都以 address error 完成。
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            phase = tlm::BEGIN_RESP;
            delay = sc_core::sc_time(kCycleNs, sc_core::SC_NS);
            return tlm::TLM_UPDATED;
        }
        return tlm::TLM_COMPLETED;
    }
};

class error_target64 : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<error_target64, 64> socket;

    explicit error_target64(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
    {
        socket.register_nb_transport_fw(this, &error_target64::nb_transport_fw);
    }

    tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay)
    {
        if (phase == tlm::BEGIN_REQ) {
            // ITCM LSU 端口占位：本测试不检查 LSU->ITCM 正常路径。
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            phase = tlm::BEGIN_RESP;
            delay = sc_core::sc_time(kCycleNs, sc_core::SC_NS);
            return tlm::TLM_UPDATED;
        }
        return tlm::TLM_COMPLETED;
    }
};

class exu_testbench : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(exu_testbench);

    e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t>& pipe;
    exu_ca& exu;
    bool observed_flush_ = false;
    e203sim::addr_t observed_flush_pc_ = 0;
    uint64_t observed_flush_epoch_ = 0;
    uint64_t seen_flush_epoch_ = 0;

    exu_testbench(sc_core::sc_module_name name,
                  e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t>& pipe_ref,
                  exu_ca& exu_ref)
        : sc_core::sc_module(name), pipe(pipe_ref), exu(exu_ref)
    {
        SC_THREAD(run);
    }

private:
    void tick(unsigned int count = 1)
    {
        // 等一个或多个 EXU cycle，并给 SystemC signal 一个 delta 更新窗口。
        for (unsigned int i = 0; i < count; ++i) {
            wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
            wait(sc_core::SC_ZERO_TIME);
            wait(sc_core::SC_ZERO_TIME);
        }
    }

    void issue(const e203sim::ifu_exu_packet& pkt, bool post_tick = true)
    {
        // valid 保持到 EXU ready，模拟 IFU packet 被成功消费。
        observed_flush_ = false;
        pipe.write(pkt);
        pipe.set_valid(true);
        tick();
        for (unsigned int i = 0; i < 8 && !pipe.ready(); ++i) {
            tick();
        }
        expect(pipe.ready(), "EXU should accept packet");
        if (pipe.flush_epoch() != seen_flush_epoch_) {
            seen_flush_epoch_ = pipe.flush_epoch();
            observed_flush_ = true;
            observed_flush_pc_ = pipe.flush_target();
            observed_flush_epoch_ = seen_flush_epoch_;
        }
        pipe.set_valid(false);
        if (post_tick) {
            tick();
        } else {
            wait(sc_core::SC_ZERO_TIME);
        }
    }

    void expect_stall(const e203sim::ifu_exu_packet& pkt)
    {
        // 用于验证 hazard/resource stall：EXU 应保持 ready=false。
        pipe.write(pkt);
        pipe.set_valid(true);
        tick();
        expect(!pipe.ready(), "EXU should stall packet");
        pipe.set_valid(false);
        tick();
    }

    void expect_muldiv_result(uint8_t rd,
                              uint32_t expected,
                              unsigned int max_cycles,
                              const std::string& msg)
    {
        for (unsigned int i = 0; i < max_cycles; ++i) {
            tick();
            if (exu.regs().read(rd) == expected) {
                return;
            }
        }
        fail(msg);
    }

    void expect_flush(uint32_t pc, const std::string& msg)
    {
        // flush 使用 epoch 表示控制事件；issue() 会锁存这一拍观察到的新 epoch。
        if (observed_flush_) {
            expect(observed_flush_epoch_ != 0, msg + ": observed invalid flush epoch");
            expect(observed_flush_pc_ == pc, msg);
            observed_flush_ = false;
            return;
        }
        for (unsigned int i = 0; i < 4; ++i) {
            if (pipe.flush_epoch() != seen_flush_epoch_) {
                seen_flush_epoch_ = pipe.flush_epoch();
                expect(pipe.flush_target() == pc, msg);
                return;
            }
            tick();
        }
        fail(msg + ": flush was not asserted");
    }

    void test_helpers()
    {
        // 先覆盖纯 C++ helper，避免所有错误都落到 SystemC 时序测试里。
        regfile rf;
        rf.write(0, 0xdeadbeefu, true);
        expect(rf.read(0) == 0, "x0 must remain zero");
        rf.write(3, 0x12345678u, true);
        expect(rf.read(3) == 0x12345678u, "regfile read/write mismatch");

        const auto add = e203sim::decode_rv32i(encode_alu(1, 2, 3, 0, 0));
        expect(add.kind == e203sim::inst_kind::Alu && add.rs1 == 2 && add.rs2 == 3 && add.rd == 1,
               "R decode mismatch");
        const auto mul = e203sim::decode_rv32i(encode_muldiv(1, 2, 3, 0));
        expect(mul.kind == e203sim::inst_kind::MulDiv && mul.muldiv == e203sim::muldiv_op::Mul,
               "M decode mismatch");
        const auto remu = e203sim::decode_rv32i(encode_muldiv(1, 2, 3, 7));
        expect(remu.kind == e203sim::inst_kind::MulDiv && remu.muldiv == e203sim::muldiv_op::Remu,
               "REMU decode mismatch");
        const auto mret = e203sim::decode_rv32i(encode_mret());
        expect(mret.kind == e203sim::inst_kind::Mret, "MRET decode mismatch");
        const auto fence_i = e203sim::decode_rv32i(encode_fence_i());
        expect(fence_i.kind == e203sim::inst_kind::Fence && !fence_i.illegal, "FENCE.I decode mismatch");
        const auto lw = e203sim::decode_rv32i(encode_lw(5, 6, 12));
        expect(lw.kind == e203sim::inst_kind::Load && lw.mem_size == e203sim::AccessSize::Word,
               "load decode mismatch");
        expect(e203sim::decode_rv32i(0xffffffffu).illegal, "illegal decode should be flagged");

        alu_unit alu;
        expect(alu.execute(e203sim::alu_op::Add, 1, 2) == 3, "ALU add mismatch");
        expect(alu.execute(e203sim::alu_op::Sub, 7, 2) == 5, "ALU sub mismatch");
        expect(alu.execute(e203sim::alu_op::Sra, 0x80000000u, 31) == 0xffffffffu, "ALU sra mismatch");
        expect(alu.branch_taken(e203sim::branch_op::Blt, 0xffffffffu, 1), "branch signed compare mismatch");
    }

    void run()
    {
        test_helpers();
        pipe.set_valid(false);
        tick(2);

        // 单拍 ALU/LUI 写回路径。
        issue(packet(0x80000000u, encode_addi(1, 0, 7)));
        expect(exu.regs().read(1) == 7, "addi writeback mismatch");
        issue(packet(0x80000004u, encode_alu(2, 1, 1, 0, 0)));
        expect(exu.regs().read(2) == 14, "add writeback mismatch");
        issue(packet(0x80000008u, 0x123450b7u));
        expect(exu.regs().read(1) == 0x12345000u, "lui writeback mismatch");

        // load 进入 OITF，未返回前读其 rd 的年轻指令必须 RAW stall。
        exu.regs().write(10, kDtcmBase, true);
        issue(packet(0x8000000cu, encode_lw(3, 10, 0)), false);
        expect(exu.oitf_size() == 1, "load should allocate OITF entry");
        expect_stall(packet(0x80000010u, encode_addi(4, 3, 1)));
        tick(3);
        expect(exu.regs().read(3) == 0xaabbccdd, "load writeback mismatch");
        expect(exu.oitf_empty(), "OITF should retire load");

        // store 无 rd 写回；随后 load 同地址验证 byte-enable/data path。
        exu.regs().write(5, 0x11223344u, true);
        issue(packet(0x80000014u, encode_sw(5, 10, 4)));
        tick(3);
        issue(packet(0x80000018u, encode_lw(6, 10, 4)));
        tick(3);
        expect(exu.regs().read(6) == 0x11223344u, "store/load readback mismatch");

        // sub-word store 必须把数据放到 lane 内地址低位对应的字节，否则 high-half 写会落成 0。
        exu.regs().write(5, 0x00000015u, true);
        issue(packet(0x8000001cu, encode_sh(5, 10, 6)));
        tick(3);
        issue(packet(0x80000020u, encode_lhu(6, 10, 6)));
        tick(3);
        expect(exu.regs().read(6) == 0x15u, "unaligned-lane halfword store/load mismatch");

        // 分支预测错误和 JAL 都应通过 pipeline flush 重定向 IFU。
        issue(packet(0x8000001cu, encode_beq(0, 0, 8), false));
        expect_flush(0x80000024u, "taken branch should flush to target when prediction was not taken");
        tick();

        issue(packet(0x80000020u, encode_jal(7, 12)));
        expect(exu.regs().read(7) == 0x80000024u, "jal link writeback mismatch");
        expect_flush(0x8000002cu, "jal flush target mismatch");
        tick();

        // CSR 指令读旧值写 rd，并更新 machine CSR 状态。
        exu.regs().write(8, 0x100u, true);
        issue(packet(0x80000024u, encode_csrrw(9, 0x305, 8)));
        expect(exu.csr().mtvec == 0x100u, "csrrw mtvec update mismatch");
        expect(exu.regs().read(9) == 0, "csrrw should write old csr value");

        exu.regs().write(8, 0x12345678u, true);
        issue(packet(0x80000028u, encode_csrrw(9, 0x340, 8)));
        expect(exu.csr().mscratch == 0x12345678u, "csrrw mscratch update mismatch");
        expect(exu.regs().read(9) == 0, "csrrw mscratch old value mismatch");

        exu.regs().write(8, 0x0000ff00u, true);
        issue(packet(0x8000002cu, encode_csr(10, 0x340, 8, 0x2)));
        expect(exu.regs().read(10) == 0x12345678u, "csrrs should return old CSR");
        expect(exu.csr().mscratch == 0x1234ff78u, "csrrs update mismatch");

        exu.regs().write(8, 0x00005600u, true);
        issue(packet(0x80000030u, encode_csr(11, 0x340, 8, 0x3)));
        expect(exu.regs().read(11) == 0x1234ff78u, "csrrc should return old CSR");
        expect(exu.csr().mscratch == 0x1234a978u, "csrrc update mismatch");

        issue(packet(0x80000034u, encode_csr(12, 0x340, 3, 0x5)));
        expect(exu.regs().read(12) == 0x1234a978u, "csrrwi should return old CSR");
        expect(exu.csr().mscratch == 3u, "csrrwi update mismatch");
        issue(packet(0x80000038u, encode_csr(13, 0x340, 4, 0x6)));
        expect(exu.regs().read(13) == 3u, "csrrsi should return old CSR");
        expect(exu.csr().mscratch == 7u, "csrrsi update mismatch");
        issue(packet(0x8000003cu, encode_csr(14, 0x340, 1, 0x7)));
        expect(exu.regs().read(14) == 7u, "csrrci should return old CSR");
        expect(exu.csr().mscratch == 6u, "csrrci update mismatch");

        issue(packet(0x80000040u, encode_csr(15, 0x340, 0, 0x2)));
        expect(exu.regs().read(15) == 6u, "csrrs x0 should return old CSR");
        expect(exu.csr().mscratch == 6u, "csrrs x0 should not write CSR");
        issue(packet(0x80000044u, encode_csr(16, 0x340, 0, 0x3)));
        expect(exu.regs().read(16) == 6u, "csrrc x0 should return old CSR");
        expect(exu.csr().mscratch == 6u, "csrrc x0 should not write CSR");

        issue(packet(0x80000048u, encode_csr(17, 0x7aa, 0, 0x2)));
        expect(exu.regs().read(17) == 0, "unknown CSR should read as zero");
        issue(packet(0x8000004cu, encode_csrrw(0, 0x7aa, 8)));
        issue(packet(0x80000050u, encode_csr(18, 0x7aa, 0, 0x2)));
        expect(exu.regs().read(18) == 0, "unknown CSR write should be ignored");

        exu.regs().write(8, 2u, true);
        issue(packet(0x80000054u, encode_csrrw(0, 0xb80, 8)));
        expect((exu.csr().mcycle >> 32u) == 2u, "mcycleh write mismatch");
        exu.regs().write(8, 1u, true);
        issue(packet(0x80000058u, encode_csrrw(0, 0xb82, 8)));
        expect((exu.csr().minstret >> 32u) == 1u, "minstreth write mismatch");

        // ecall 在 commit 侧更新 mepc/mcause，并 flush 到 mtvec。
        exu.regs().write(8, 0x8u, true);
        issue(packet(0x8000005cu, encode_csrrw(0, 0x300, 8)));
        issue(packet(0x80000060u, 0x00000073u));
        expect(exu.csr().mepc == 0x80000060u, "ecall mepc mismatch");
        expect(exu.csr().mcause == 11u, "ecall mcause mismatch");
        expect((exu.csr().mstatus & 0x88u) == 0x80u, "ecall mstatus update mismatch");
        expect_flush(0x100u, "trap flush should use mtvec");
        tick();

        issue(packet(0x80000064u, encode_mret()));
        expect((exu.csr().mstatus & 0x88u) == 0x88u, "mret mstatus restore mismatch");
        expect_flush(0x80000060u, "mret should flush to mepc");
        tick();

        issue(packet(0x80000068u, encode_fence_i()));
        expect_flush(0x8000006cu, "fence.i should flush to next pc");
        tick();

        // 人工插入 OITF entry，验证 WAW/RAW hazard 检测不会依赖 LSU 返回。
        expect(exu.debug_reserve_oitf(20, true, 0x9000), "debug OITF reserve should succeed");
        expect_stall(packet(0x8000002cu, encode_addi(21, 20, 1)));
        expect_stall(packet(0x8000002cu, encode_csr(21, 0x340, 0, 0x2)));
        expect_stall(packet(0x8000002cu, encode_fence_i()));
        expect_stall(packet(0x8000002cu, encode_mret()));

        // RV32M 是 EXU 内部阻塞阶段：不进 OITF，busy 时年轻 ALU 不能越过。
        exu.regs().write(11, 7, true);
        exu.regs().write(12, 6, true);
        issue(packet(0x80000030u, encode_muldiv(13, 11, 12, 0)), false);
        expect(exu.oitf_size() == 1, "debug OITF entry should be unrelated to MULDIV");
        pipe.write(packet(0x80000034u, encode_addi(14, 0, 1)));
        pipe.set_valid(true);
        tick();
        expect(!pipe.ready(), "MULDIV should block younger ALU dispatch");
        pipe.set_valid(false);
        expect_muldiv_result(13, 42, 20, "mul result/latency mismatch");

        // signed high/unsigned high 和除法特殊情况。
        exu.regs().write(15, 0xffffffffu, true);
        exu.regs().write(16, 2, true);
        issue(packet(0x80000038u, encode_muldiv(17, 15, 16, 1)), false);
        expect_muldiv_result(17, 0xffffffffu, 20, "mulh result mismatch");
        issue(packet(0x8000003cu, encode_muldiv(18, 15, 16, 3)), false);
        expect_muldiv_result(18, 1, 20, "mulhu result mismatch");
        issue(packet(0x80000040u, encode_muldiv(19, 15, 0, 4)), false);
        expect_muldiv_result(19, 0xffffffffu, 4, "div-by-zero quotient mismatch");
        issue(packet(0x80000044u, encode_muldiv(22, 15, 0, 6)), false);
        expect_muldiv_result(22, 0xffffffffu, 4, "rem-by-zero remainder mismatch");

        // 普通除法整除走较短普通路径，非整除走 correction 路径。
        exu.regs().write(23, 84, true);
        exu.regs().write(24, 7, true);
        issue(packet(0x80000048u, encode_muldiv(25, 23, 24, 4)), false);
        expect_muldiv_result(25, 12, 35, "div exact result mismatch");
        exu.regs().write(23, 85, true);
        issue(packet(0x8000004cu, encode_muldiv(26, 23, 24, 6)), false);
        expect_muldiv_result(26, 1, 37, "rem correction result mismatch");

        // b2b fuse：MULH 后接 MUL，第二条 packet 标记 b2b 后只用缓存低 32bit。
        exu.regs().write(27, 0x12345678u, true);
        exu.regs().write(28, 0x10u, true);
        issue(packet(0x80000050u, encode_muldiv(29, 27, 28, 1)), false);
        expect_muldiv_result(29, 0x00000001u, 20, "mulh b2b producer mismatch");
        issue(b2b_packet(0x80000054u, encode_muldiv(30, 27, 28, 0)), false);
        expect_muldiv_result(30, 0x23456780u, 3, "mul b2b result mismatch");

        std::cout << "[TEST][PASS] exu_ca component test passed" << std::endl;
        sc_core::sc_stop();
    }
};

} // namespace

int sc_main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    e203sim::memory_config dtcm_cfg;
    dtcm_cfg.name = "dtcm";
    dtcm_cfg.base_addr = kDtcmBase;
    dtcm_cfg.size = kDtcmSize;
    dtcm_cfg.read_latency_cycles = 1;
    dtcm_cfg.write_latency_cycles = 1;

    e203sim::memory_config itcm_cfg;
    itcm_cfg.name = "itcm";
    itcm_cfg.base_addr = 0x80000000u;
    itcm_cfg.size = 0x10000u;

    e203sim::sim_config sim_cfg{};
    sim_cfg.cycle_ns = kCycleNs;
    sim_cfg.enable_debug = false;
    sim_cfg.dtcm = &dtcm_cfg;
    sim_cfg.itcm = &itcm_cfg;

    e203sim::debug_logger::instance().disable();

    dtcm_ca dtcm("dtcm", &dtcm_cfg, kCycleNs);
    // DTCM 预装首个 load 读回值。
    expect(dtcm.sram_write(0, 0xaabbccddu, 0x0fu), "failed to preload DTCM");

    e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t> pipe("ifu_exu_pipe");
    exu_ca exu("exu", sim_cfg);
    error_target64 itcm_error("itcm_error");
    error_target biu_error("biu_error");
    exu.bind_pipeline(pipe);
    // EXU.LSU 只把 DTCM 正常绑定；ITCM/BIU 绑定错误 target 以捕获误访问。
    exu.lsu().lsu2dtcm_initiator_socket.bind(dtcm.lsu2dtcm_target_socket);
    exu.lsu().lsu2itcm_initiator_socket.bind(itcm_error.socket);
    exu.lsu().lsu2biu_initiator_socket.bind(biu_error.socket);

    exu_testbench testbench("exu_testbench", pipe, exu);
    sc_core::sc_start();
    return 0;
}
