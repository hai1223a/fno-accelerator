#include "models/core/lsu_ca.h"

#include "common/debug_logger.h"
#include "sim/thread_trace.h"

#include <cstdlib>
#include <iostream>

using namespace sc_core;

namespace {

// LSU 与 SRAM target 之间使用 32-bit lane 小端数据布局。
uint32_t load_le32(const std::array<uint8_t, 4>& data)
{
    return (static_cast<uint32_t>(data[0]) << 0) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void store_le32(std::array<uint8_t, 4>& data, uint32_t value)
{
    data[0] = static_cast<uint8_t>((value >> 0) & 0xff);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xff);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xff);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

uint32_t sign_extend(uint32_t value, unsigned int bits)
{
    // load byte/halfword 的有符号扩展，返回 RV32 写回数据。
    const uint32_t sign = 1u << (bits - 1u);
    return (value ^ sign) - sign;
}

uint32_t make_store_lane(e203sim::AccessSize size, uint32_t store_data)
{
    switch (size) {
    case e203sim::AccessSize::Byte: {
        const uint32_t byte = store_data & 0xffu;
        return byte | (byte << 8u) | (byte << 16u) | (byte << 24u);
    }
    case e203sim::AccessSize::HalfWord: {
        const uint32_t half = store_data & 0xffffu;
        return half | (half << 16u);
    }
    case e203sim::AccessSize::Word:
        return store_data;
    }
    return store_data;
}

} // namespace

lsu_ca::lsu_ca(sc_module_name module_name, const e203sim::sim_config &config)
    : sc_module(module_name), clk_period_(config.cycle_ns, SC_NS)
{
    memories_[0] = config.dtcm;
    memories_[1] = config.itcm;

    lsu2dtcm_initiator_socket.register_nb_transport_bw(this, &lsu_ca::nb_transport_bw);
    lsu2itcm_initiator_socket.register_nb_transport_bw(this, &lsu_ca::nb_transport_bw);
    lsu2biu_initiator_socket.register_nb_transport_bw(this, &lsu_ca::nb_transport_bw);
    SC_THREAD(thread);
    INFO(module_name << " created !");
}

lsu_ca::~lsu_ca() = default;

bool lsu_ca::request_ready() const
{
    // v1 LSU 结构化限制：队列、context、nb_transport outstanding 都为空才接收新请求。
    return request_q_.empty() && outstanding_ == nullptr && !has_nb_outstanding();
}

bool lsu_ca::issue(const request& req)
{
    if (!request_ready()) {
        return false;
    }
    request_q_.push(req);
    request_event_.notify(SC_ZERO_TIME);
    return true;
}

bool lsu_ca::response_valid() const
{
    return !response_q_.empty();
}

lsu_ca::response lsu_ca::take_response()
{
    SIM_ASSERT(!response_q_.empty(), "LSU response queue is empty");
    response rsp = response_q_.front();
    response_q_.pop();
    return rsp;
}

const sc_event& lsu_ca::response_event() const
{
    return response_event_;
}

tlm::tlm_sync_enum lsu_ca::nb_transport_bw(tlm::tlm_generic_payload& trans,
                                           tlm::tlm_phase& phase,
                                           sc_time& delay)
{
    (void)delay;
    if (phase == tlm::BEGIN_RESP) {
        // target 在 BEGIN_RESP 返回最终状态；LSU 转成 response 后回 END_RESP 完成事务。
        handle_response(trans);
        complete_nb_begin_resp(trans, phase, basename());
        outstanding_.reset();
        response_event_.notify(SC_ZERO_TIME);
        request_event_.notify(SC_ZERO_TIME);
        return tlm::TLM_COMPLETED;
    }
    return tlm::TLM_ACCEPTED;
}

void lsu_ca::handle_response(tlm::tlm_generic_payload& trans)
{
    SIM_ASSERT(outstanding_, "LSU received response without outstanding request");
    SIM_ASSERT(&outstanding_->trans == &trans, "LSU response payload mismatch");

    response rsp;
    // itag/pc/rd 从原请求透传，EXU 用于 OITF 匹配和异常提交。
    rsp.itag = outstanding_->req.itag;
    rsp.pc = outstanding_->req.pc;
    rsp.addr = outstanding_->req.addr;
    rsp.rd = outstanding_->req.rd;
    rsp.instr = outstanding_->req.instr;
    rsp.trace_seq = outstanding_->req.trace_seq;
    rsp.trace_fetch_tick = outstanding_->req.trace_fetch_tick;
    rsp.trace_label = outstanding_->req.trace_label;
    rsp.load = outstanding_->req.load;
    rsp.rd_write = outstanding_->req.rd_write && outstanding_->req.load;
    rsp.size = outstanding_->req.size;
    rsp.store_data = outstanding_->req.store_data;
    rsp.status = trans.get_response_status();
    rsp.error = trans.is_response_error();
    rsp.badaddr = outstanding_->req.addr;

    if (outstanding_->req.load && !rsp.error) {
        // SRAM 返回完整 32-bit lane；这里按原始地址低位提取 byte/halfword。
        const uint32_t lane = load_le32(outstanding_->data);
        const uint32_t shift = (outstanding_->req.addr & 0x3u) * 8u;
        switch (outstanding_->req.size) {
        case e203sim::AccessSize::Byte: {
            const uint32_t raw = (lane >> shift) & 0xffu;
            rsp.load_data = outstanding_->req.unsigned_load ? raw : sign_extend(raw, 8);
            break;
        }
        case e203sim::AccessSize::HalfWord: {
            const uint32_t raw = (lane >> shift) & 0xffffu;
            rsp.load_data = outstanding_->req.unsigned_load ? raw : sign_extend(raw, 16);
            break;
        }
        case e203sim::AccessSize::Word:
            rsp.load_data = lane;
            break;
        }
    }

    response_q_.push(rsp);
    THREAD_TRACE("LSU访存线程", "接收响应",
                 "PC=0x" << std::hex << rsp.pc
                 << " 地址=0x" << rsp.addr
                 << " itag=" << std::dec << rsp.itag
                 << " 错误=" << rsp.error);
}

std::unique_ptr<lsu_ca::trans_ctx> lsu_ca::make_transaction(const request& req) const
{
    auto ctx = std::make_unique<trans_ctx>();
    ctx->req = req;
    // e203 写数据通路会把 byte/halfword store 数据复制到 32-bit lane；
    // byte_enable 再按地址低位选择真正写入的字节。
    store_le32(ctx->data, make_store_lane(req.size, req.store_data));

    ctx->byte_enable = {0, 0, 0, 0};
    // DTCM/ITCM target 当前以 4-byte data_length 建模局部写；sub-word store
    // 通过 byte_enable 精确选择 lane 内字节。
    switch (req.size) {
    case e203sim::AccessSize::Byte:
        ctx->byte_enable[req.addr & 0x3u] = 1;
        break;
    case e203sim::AccessSize::HalfWord:
        if ((req.addr & 0x2u) == 0) {
            ctx->byte_enable = {1, 1, 0, 0};
        } else {
            ctx->byte_enable = {0, 0, 1, 1};
        }
        break;
    case e203sim::AccessSize::Word:
        ctx->byte_enable = {1, 1, 1, 1};
        break;
    }

    ctx->trans.set_command(req.load ? tlm::TLM_READ_COMMAND : tlm::TLM_WRITE_COMMAND);
    // payload 字段在发起 BEGIN_REQ 前完整设置；response_status 初始为 incomplete。
    ctx->trans.set_address(req.addr);
    ctx->trans.set_data_ptr(ctx->data.data());
    ctx->trans.set_data_length(4);
    ctx->trans.set_byte_enable_ptr(req.load ? nullptr : ctx->byte_enable.data());
    ctx->trans.set_byte_enable_length(req.load ? 0 : 4);
    ctx->trans.set_streaming_width(4);
    ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return ctx;
}

int lsu_ca::decode_memory_index(uint32_t addr) const
{
    // 返回 0=DTCM、1=ITCM、-1=BIU。空 config 视为未命中。
    for (std::size_t i = 0; i < memories_.size(); ++i) {
        const auto* mem = memories_[i];
        if (mem != nullptr && addr >= mem->base_addr && addr < mem->base_addr + mem->size) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void lsu_ca::issue_request_to_memory(std::unique_ptr<trans_ctx> ctx)
{
    SIM_ASSERT(!outstanding_ && !has_nb_outstanding(), "LSU only supports one outstanding request");

    const int hit = decode_memory_index(ctx->req.addr);
    outstanding_ = std::move(ctx);
    // phase 时序：LSU 发 BEGIN_REQ；target 接受后返回 END_REQ，稍后 BEGIN_RESP。
    if (hit == 0) {
        send_nb_begin_req(lsu2dtcm_initiator_socket, outstanding_->trans, basename(), "LSU->DTCM");
    } else if (hit == 1) {
        send_nb_begin_req(lsu2itcm_initiator_socket, outstanding_->trans, basename(), "LSU->ITCM");
    } else {
        send_nb_begin_req(lsu2biu_initiator_socket, outstanding_->trans, basename(), "LSU->BIU");
    }
}

void lsu_ca::thread()
{
    while (true) {
        wait(request_event_);
        THREAD_TRACE("LSU访存线程", "线程唤醒", "请求队列=" << request_q_.size()
                     << " 未完成请求=" << static_cast<bool>(outstanding_));
        while (!request_q_.empty() && outstanding_ == nullptr && !has_nb_outstanding()) {
            request req = request_q_.front();
            request_q_.pop();
            THREAD_TRACE("LSU访存线程", "取出请求", "PC=0x" << std::hex << req.pc
                         << " 地址=0x" << req.addr << std::dec
                         << " itag=" << req.itag << " 读请求=" << req.load);

            // 地址对齐属于 AGU/LSU 同步错误，不发 TLM transaction，直接返回错误完成包。
            if ((req.size == e203sim::AccessSize::HalfWord && (req.addr & 0x1u) != 0) ||
                (req.size == e203sim::AccessSize::Word && (req.addr & 0x3u) != 0)) {
                response rsp;
                rsp.itag = req.itag;
                rsp.pc = req.pc;
                rsp.addr = req.addr;
                rsp.rd = req.rd;
                rsp.instr = req.instr;
                rsp.trace_seq = req.trace_seq;
                rsp.trace_fetch_tick = req.trace_fetch_tick;
                rsp.load = req.load;
                rsp.rd_write = false;
                rsp.size = req.size;
                rsp.store_data = req.store_data;
                rsp.error = true;
                rsp.badaddr = req.addr;
                rsp.status = tlm::TLM_ADDRESS_ERROR_RESPONSE;
                response_q_.push(rsp);
                response_event_.notify(SC_ZERO_TIME);
                THREAD_TRACE("LSU访存线程", "返回未对齐错误", "地址=0x" << std::hex << req.addr << std::dec);
                continue;
            }

            issue_request_to_memory(make_transaction(req));
            THREAD_TRACE("LSU访存线程", "等待响应", "itag=" << req.itag);
            // 保持单 outstanding：等待响应释放 context 后再处理后续请求。
            wait(response_event_ | request_event_);
        }
    }
}
