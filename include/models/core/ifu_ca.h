#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "common/pipeline_if.h"
#include "common/tlm_nb_transport.h"
#include "common/types.h"

class ifu_ca : public sc_core::sc_module, protected e203sim::nb_transport_initiator_base
{
private:
    // 保存一次 non-blocking ITCM 读请求的 payload 和返回数据缓冲。
    // payload 生命周期必须覆盖 BEGIN_REQ 到 BEGIN_RESP。
    struct trans_ctx {
        tlm::tlm_generic_payload trans;
        std::array<uint8_t, 8> data{};
        e203sim::addr_t addr = 0;
        uint64_t generation = 0;
    };

    // mini-decode 的内部结果，只服务 IFU 本地 PC 推进和预测逻辑。
    struct mini_decode_result {
        bool instr_32bit = false;
        bool jal = false;
        bool jalr = false;
        bool branch = false;
        bool mul = false;
        bool mulh = false;
        bool mulhsu = false;
        bool mulhu = false;
        bool div = false;
        bool divu = false;
        bool rem = false;
        bool remu = false;
        int32_t bjp_imm = 0;
        uint32_t rs1idx = 0;
        uint32_t rs2idx = 0;
        uint32_t rdidx = 0;
    };

    // 当前模型的轻量分支预测结果。
    struct branch_prediction {
        bool taken = false;
        e203sim::addr_t target_pc = 0;
    };

    // 缓存最近一次从 ITCM 取回的 64-bit lane。
    // 后续 PC 仍落在该 lane 时直接复用；跨 lane 时读下一条并更新缓存。
    struct leftover_buffer {
        struct entry {
            bool valid = false;
            e203sim::addr_t lane_addr = 0;
            uint64_t lane_data = 0;
            tlm::tlm_response_status status = tlm::TLM_INCOMPLETE_RESPONSE;
            bool just_received = false;
        };
        std::deque<entry> entries;
    };

    std::unique_ptr<trans_ctx> outstanding_;
    e203sim::memory_config itcm_config_;
    sc_core::sc_time clk_period_;
    e203sim::addr_t pc_;
    // IFU 内部最多保留当前 IR 和 response bypass 中的下一条候选指令。
    // 这对应 RTL 中 IR 寄存器加 ifetch response bypass buffer 的吞吐行为。
    std::deque<e203sim::ifu_exu_packet> fetch_queue_;
    leftover_buffer leftover_;
    sc_core::sc_event response_event_;
    uint64_t response_lane_ = 0;
    e203sim::addr_t response_lane_addr_ = 0;
    tlm::tlm_response_status response_status_ = tlm::TLM_INCOMPLETE_RESPONSE;
    bool response_pending_ = false;
    bool delayed_flush_pending_ = false;
    e203sim::addr_t delayed_flush_pc_ = 0;
    uint64_t seen_flush_epoch_ = 0;
    bool candidate_bus_error_ = false;
    uint64_t fetch_generation_ = 0;
    uint64_t next_trace_fetch_tick_ = 0;
    bool last_muldiv_valid_ = false;
    mini_decode_result last_muldiv_dec_{};

    // IFU 主线程：处理 flush、IR buffer 交付、取指、mini-decode 和下一 PC 预测。
    void fetch_thread();

    // 尝试填充 IFU 内部队列；命中 hold-up lane 时可连续生成，miss 时只发起请求。
    void fill_fetch_queue();

    // 为 Konata/O3PipeView 分配单发射可视化 fetch 槽位，避免内部 0uop 同拍显示重叠。
    uint64_t allocate_trace_fetch_tick();

    // 若 ITCM response 已回来，将 lane 写入 hold-up cache。
    void consume_pending_response();

    // 按 RTL flush 语义清空 IFU 本地状态；能发新请求则立即切到目标 PC，否则记录 delayed flush。
    void handle_flush_request(bool delayed);

    // 当前是否没有在途 IFU request，可以立刻发起 flush target 取指。
    bool can_issue_fetch_request() const;

    // 线程唤醒日志使用的 PC：表示本轮 IFU 预计继续推进的取指 PC。
    e203sim::addr_t wakeup_trace_pc() const;

    // 尝试用当前 hold-up lane 生成一条候选指令；缺 lane 时发起 ITCM 请求并返回 false。
    // from_new_lane 表示本条指令使用了本周期刚从 ITCM 返回的 lane。
    bool try_make_candidate(e203sim::addr_t pc,
                            uint32_t& candidate,
                            bool& bus_error,
                            bool& from_new_lane,
                            e203sim::addr_t& lane_addr,
                            e203sim::addr_t& next_lane_addr);

    // 本轮填队列结束后，已接收 lane 后续再命中时显示为普通命中。
    void clear_just_received_lanes();

    // 发起一次 64-bit ITCM lane 读取，并等待 BEGIN_RESP 返回。
    uint64_t read_lane(e203sim::addr_t addr);

    // 从 leftover buffer 或 ITCM 获取指定 64-bit lane。
    uint64_t get_lane(e203sim::addr_t lane_addr);

    // hold-up lane 命中判断。
    bool has_lane(e203sim::addr_t lane_addr) const;

    // 读取 hold-up lane 内容；调用前必须确认 has_lane 为真。
    const leftover_buffer::entry& lookup_lane(e203sim::addr_t lane_addr) const;

    // 发起指定 PC 所在 lane 的读取；若已有 outstanding 则保持等待。
    void request_lane(e203sim::addr_t pc, e203sim::addr_t lane_addr);

    // 基于 PC 生成最多 32-bit 的指令候选值，必要时跨 lane 拼接。
    uint32_t make_candidate(e203sim::addr_t pc);

    // 识别 16/32-bit 长度，并提取 IFU 预测需要的少量字段。
    mini_decode_result mini_decode(uint32_t instr) const;

    // 使用简化策略生成预测结果：JAL/JALR taken，后跳 branch taken。
    branch_prediction branch_predict(e203sim::addr_t pc, const mini_decode_result& dec) const;

    // e203 IFU 中的 MULDIV back-to-back 轻量识别。
    bool is_muldiv_b2b(const mini_decode_result& dec) const;
    void update_muldiv_b2b_state(const mini_decode_result& dec);

    // 将 IFU 本地 decode/预测结果封装为跨模块共享的 IFU->EXU packet。
    e203sim::ifu_exu_packet build_packet(e203sim::addr_t pc,
                                         uint32_t instr,
                                         const mini_decode_result& dec,
                                         const branch_prediction& pred,
                                         bool bus_error,
                                         bool misalign) const;

    // 把 fetch queue 队首驱动到通用流水线生产者接口，作为下一拍对 EXU 可见的 packet。
    void drive_fetch_queue();

    // 清空流水线前向输出，表示当前没有有效 packet。
    void clear_pipeline_output();

    // 发出 BEGIN_REQ，不等待响应；响应由 nb_transport_bw 唤醒 fetch 线程。
    void send_read64(std::unique_ptr<trans_ctx> ctx);

    // 处理 ITCM BEGIN_RESP，记录 response status 和 64-bit lane 数据。
    void handle_response(tlm::tlm_generic_payload& trans);

    // 构造一次 64-bit ITCM 读 transaction。
    std::unique_ptr<trans_ctx> make_read64(e203sim::addr_t addr);

public:
    SC_HAS_PROCESS(ifu_ca);

    tlm_utils::simple_initiator_socket<ifu_ca, 64> ifu2itcm_initiator_socket;

    sc_core::sc_port<e203sim::pipeline_producer_if<e203sim::ifu_exu_packet, e203sim::addr_t>> ifu_pipeline_port;

    ifu_ca(sc_core::sc_module_name module_name, const e203sim::sim_config& config);
    ~ifu_ca();

    // 兼容旧测试入口：直接发起一次 lane read，不参与正常 fetch 流。
    void issue_read32(uint32_t addr);

    // 绑定到可复用流水线通道；IFU 作为 producer，后级作为 consumer。
    void bind_pipeline(e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t>& pipe);

    tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay);
};
