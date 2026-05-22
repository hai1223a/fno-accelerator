#pragma once

#include "common/tlm_nb_transport.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

class lsu_ca : public sc_core::sc_module, protected e203sim::nb_transport_initiator_base
{
public:
    // EXU 发给 LSU 的 long-pipe 请求。itag 与 OITF entry 一一对应，
    // LSU 原样带回 response，EXU 用它检查返回顺序。
    struct request {
        uint32_t itag = 0;
        uint32_t pc = 0;
        uint32_t addr = 0;
        uint32_t store_data = 0;
        uint8_t rd = 0;
        uint32_t instr = 0;
        uint64_t trace_seq = 0;
        uint64_t trace_fetch_tick = 0;
        std::string trace_label;
        e203sim::AccessSize size = e203sim::AccessSize::Word;
        bool load = true;
        bool unsigned_load = false;
        bool rd_write = false;
    };

    // LSU 返回给 EXU 的完成包。load_data 已按访问宽度完成符号/零扩展；
    // error/badaddr 用于 commit 侧生成 load/store trap。
    struct response {
        uint32_t itag = 0;
        uint32_t pc = 0;
        uint32_t addr = 0;
        uint32_t load_data = 0;
        uint8_t rd = 0;
        uint32_t instr = 0;
        uint64_t trace_seq = 0;
        uint64_t trace_fetch_tick = 0;
        std::string trace_label;
        bool load = true;
        bool rd_write = false;
        e203sim::AccessSize size = e203sim::AccessSize::Word;
        uint32_t store_data = 0;
        bool error = false;
        uint32_t badaddr = 0;
        tlm::tlm_response_status status = tlm::TLM_INCOMPLETE_RESPONSE;
    };

private:
    // 保存一次 non-blocking TLM transaction 的完整生命周期。
    // data/byte_enable 必须和 payload 一起存活到 BEGIN_RESP 完成。
    struct trans_ctx {
        tlm::tlm_generic_payload trans;
        request req;
        std::array<uint8_t, 4> data{};
        std::array<uint8_t, 4> byte_enable{};
    };

    sc_core::sc_time clk_period_;
    // v1 按地址范围选择 DTCM/ITCM，否则走 BIU。
    std::array<e203sim::memory_config*, 2> memories_{};
    // EXU 与 LSU 的轻量队列接口。当前实现只接受一个待发请求和一个
    // TLM outstanding，以保持 CA v1 的时序和 payload 生命周期简单。
    std::queue<request> request_q_;
    std::queue<response> response_q_;
    std::unique_ptr<trans_ctx> outstanding_;
    sc_core::sc_event request_event_;
    sc_core::sc_event response_event_;

    void thread();
    void issue_request_to_memory(std::unique_ptr<trans_ctx> ctx);
    void handle_response(tlm::tlm_generic_payload& trans);
    std::unique_ptr<trans_ctx> make_transaction(const request& req) const;
    int decode_memory_index(uint32_t addr) const;

public:
    SC_HAS_PROCESS(lsu_ca);

    tlm_utils::simple_initiator_socket<lsu_ca> lsu2dtcm_initiator_socket;
    tlm_utils::simple_initiator_socket<lsu_ca, 64> lsu2itcm_initiator_socket;
    tlm_utils::simple_initiator_socket<lsu_ca> lsu2biu_initiator_socket;

    lsu_ca(sc_core::sc_module_name module_name, const e203sim::sim_config &config);
    ~lsu_ca();

    // request_ready 表示 LSU 可以在本周期接受一个新的 EXU 请求。
    bool request_ready() const;
    // issue 只入队 EXU 请求；真正 TLM BEGIN_REQ 由 LSU thread 发起。
    bool issue(const request& req);
    // response_valid/take_response 为 EXU commit 提供轮询式返回接口。
    bool response_valid() const;
    response take_response();
    // 暴露事件给测试或后续同步扩展使用；EXU v1 主循环当前使用轮询。
    const sc_core::sc_event& response_event() const;

    // TLM backward path：target 返回 BEGIN_RESP 时转换为 LSU response。
    tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay);
};
