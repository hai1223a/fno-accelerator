#pragma once

#include "common/pipeline_if.h"
#include "common/difftest.h"
#include "common/types.h"
#include "isa/rv32i_decode.h"
#include "models/core/alu_unit.h"
#include "models/core/lsu_ca.h"
#include "models/core/regfile.h"

#include <cstdint>
#include <deque>
#include <string>
#include <systemc>

class exu_ca : public sc_core::sc_module
{
public:
    // v1 machine CSR 集合。只保存 EXU/commit 当前需要的最小架构状态。
    struct csr_state {
        uint32_t mstatus = 0;
        uint32_t mie = 0;
        uint32_t mip = 0;
        uint32_t mtvec = 0;
        uint32_t mscratch = 0;
        uint32_t mepc = 0;
        uint32_t mcause = 0;
        uint32_t mtval = 0;
        uint64_t mcycle = 0;
        uint64_t minstret = 0;
        uint32_t counterstop = 0;
        uint32_t mcgstop = 0;
        uint32_t itcmnohold = 0;
        uint32_t mdvnob2b = 0;
    };

private:
    // Outstanding Instruction Track FIFO entry。
    // load/store dispatch 时分配；LSU response 必须匹配队首 itag 才能提交。
    struct oitf_entry {
        uint32_t itag = 0;
        uint8_t rd = 0;
        bool rd_write = false;
        uint32_t pc = 0;
        e203sim::inst_kind kind = e203sim::inst_kind::Illegal;
    };

    // EXU 内部统一提交包。普通指令更新 minstret；trap 更新 CSR 并触发 flush。
    struct commit_result {
        bool valid = false;
        bool trap = false;
        uint32_t trap_cause = 0;
        uint32_t trap_value = 0;
        uint32_t pc = 0;
        uint32_t instr = 0;
        uint64_t trace_seq = 0;
        uint64_t trace_fetch_tick = 0;
        std::string trace_label;
        bool trace_valid = false;
        bool flush = false;
        uint32_t flush_pc = 0;
        uint32_t next_pc = 0;
        e203sim::diff_store_event store{};
        bool has_store = false;
        bool skip_difftest_ref = false;
        std::string skip_difftest_reason;
    };

    struct muldiv_context {
        bool busy = false;
        e203sim::muldiv_op op = e203sim::muldiv_op::None;
        uint8_t rd = 0;
        bool rd_write = false;
        uint32_t result = 0;
        uint32_t remaining_cycles = 0;
        uint32_t pc = 0;
        uint32_t instr = 0;
        uint64_t trace_seq = 0;
        uint64_t trace_fetch_tick = 0;
        std::string trace_label;
    };

    struct muldiv_b2b_cache {
        bool valid = false;
        e203sim::muldiv_op op = e203sim::muldiv_op::None;
        uint32_t rs1 = 0;
        uint32_t rs2 = 0;
        uint32_t product_low = 0;
        uint32_t quotient = 0;
        uint32_t remainder = 0;
    };

    static constexpr std::size_t kOitfDepth = 4;

    sc_core::sc_time clk_period_;
    // EXU 拥有 LSU 子模块；core_ca 只通过 exu.lsu() 绑定外部 memory sockets。
    lsu_ca* lsu_ = nullptr;
    regfile regs_;
    alu_unit alu_;
    csr_state csr_;
    // v1 只跟踪 long-pipe load/store，用于 RAW/WAW stall 和返回顺序检查。
    std::deque<oitf_entry> oitf_;
    uint32_t next_itag_ = 1;
    // flush_pending_ 暂存 commit 侧重定向；EXU 在本周期 dispatch/commit 结束后立即驱动 IFU flush。
    bool flush_pending_ = false;
    uint32_t flush_pc_ = 0;
    e203sim::memory_config itcm_config_{};
    e203sim::memory_config dtcm_config_{};
    // SystemC 同拍调度顺序可能让 IFU 在下一拍才看到 ready。
    // 该标志保证同一个 valid packet 只被 EXU 消费一次。
    bool waiting_packet_clear_ = false;
    e203sim::ifu_exu_packet last_consumed_packet_{};
    muldiv_context muldiv_;
    muldiv_b2b_cache muldiv_b2b_cache_;

    // IFU->EXU pipeline 消费端：EXU 驱动 ready 与 flush，读取 IFU packet。
    sc_core::sc_port<e203sim::pipeline_consumer_if<e203sim::ifu_exu_packet, e203sim::addr_t>> ifu_pipeline_port;

    // EXU 主循环：按 cycle 处理 flush、LSU writeback、dispatch。
    void thread();
    // 判断 decode 后的指令是否可进入 EXU/LSU。
    bool can_dispatch(const e203sim::decoded_inst& dec) const;
    // 检查当前指令与 OITF 中未完成 long-pipe 指令的 RAW/WAW hazard。
    bool has_oitf_hazard(const e203sim::decoded_inst& dec) const;
    // 执行一次 IFU packet 的 decode、执行或 long-pipe 发射。
    void dispatch_packet(const e203sim::ifu_exu_packet& pkt);
    // 处理 LSU 返回，优先于同周期普通 ALU dispatch。
    void handle_lsu_response(const lsu_ca::response& rsp);
    // 推进 EXU 内部 MULDIV 阻塞阶段，完成时写回并提交。
    bool step_muldiv();
    // 启动一次 MULDIV 多周期操作。
    void start_muldiv(const e203sim::decoded_inst& dec,
                      const e203sim::ifu_exu_packet& pkt,
                      uint32_t rs1,
                      uint32_t rs2);
    // MULDIV 结果与延迟 helper。
    uint32_t execute_muldiv(e203sim::muldiv_op op, uint32_t rs1, uint32_t rs2) const;
    uint32_t muldiv_latency(e203sim::muldiv_op op,
                            uint32_t rs1,
                            uint32_t rs2,
                            bool b2b_hit) const;
    bool div_special_case(e203sim::muldiv_op op, uint32_t rs1, uint32_t rs2) const;
    bool div_needs_correction(e203sim::muldiv_op op, uint32_t rs1, uint32_t rs2) const;
    bool muldiv_b2b_hit(e203sim::muldiv_op op, uint32_t rs1, uint32_t rs2, bool packet_b2b) const;
    void update_muldiv_b2b_cache(e203sim::muldiv_op op, uint32_t rs1, uint32_t rs2);
    void clear_muldiv_state();
    // 统一提交入口：更新 CSR/minstret，并登记需要发往 IFU 的 flush PC。
    void commit(const commit_result& result);
    void difftest_commit(const commit_result& result, uint32_t next_pc);
    bool in_tcm(uint32_t addr) const;
    // v1 CSR 读写访问器；未知 CSR 读 0、写忽略。
    uint32_t read_csr(uint16_t addr) const;
    void write_csr(uint16_t addr, uint32_t value);
    void enter_trap(uint32_t pc, uint32_t cause, uint32_t value);
    void retire_mret();

public:
    SC_HAS_PROCESS(exu_ca);

    exu_ca(sc_core::sc_module_name module_name, const e203sim::sim_config& config);
    ~exu_ca();

    // 绑定 IFU->EXU pipeline channel。
    void bind_pipeline(e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t>& pipe);
    // 暴露 LSU 子模块给 core_ca 绑定 DTCM/ITCM/BIU sockets。
    lsu_ca& lsu();
    const lsu_ca& lsu() const;
    // 测试可见的架构状态访问器。
    regfile& regs();
    const regfile& regs() const;
    const csr_state& csr() const;
    // OITF 状态用于 component test 检查 long-pipe hazard 行为。
    bool oitf_empty() const;
    std::size_t oitf_size() const;
    // 测试辅助：人工预留 OITF entry，验证 RAW/WAW stall。
    bool debug_reserve_oitf(uint8_t rd, bool rd_write, uint32_t pc);
};
