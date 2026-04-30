#include "models/core/lsu_ca.h"
#include "common/debug_logger.h"

using namespace sc_core;

namespace {

const char* tlm_phase_name(const tlm::tlm_phase& phase)
{
    if (phase == tlm::BEGIN_REQ) {
        return "BEGIN_REQ";
    }
    if (phase == tlm::END_REQ) {
        return "END_REQ";
    }
    if (phase == tlm::BEGIN_RESP) {
        return "BEGIN_RESP";
    }
    if (phase == tlm::END_RESP) {
        return "END_RESP";
    }
    return "UNKNOWN_PHASE";
}

const char* tlm_sync_name(tlm::tlm_sync_enum value)
{
    switch (value) {
    case tlm::TLM_ACCEPTED:
        return "TLM_ACCEPTED";
    case tlm::TLM_UPDATED:
        return "TLM_UPDATED";
    case tlm::TLM_COMPLETED:
        return "TLM_COMPLETED";
    }
    return "UNKNOWN_SYNC";
}

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

tlm::tlm_sync_enum lsu_ca::nb_transport_bw(tlm::tlm_generic_payload& trans,
                                           tlm::tlm_phase& phase,
                                           sc_core::sc_time& delay)
{
     (void)delay;
    if (phase == tlm::BEGIN_RESP) {
        SIM_INFO(basename(), "lsu收到BEGIN_RESP"
                        << ", addr=0x" << std::hex << trans.get_address() << std::dec);
        phase = tlm::END_RESP;
        handle_response(trans);
        SIM_INFO(basename(), "lsu返回END_RESP，事务完成");
        return tlm::TLM_COMPLETED;
    }
    return tlm::TLM_ACCEPTED;
}

void lsu_ca::handle_response(tlm::tlm_generic_payload& trans)
{
    SIM_ASSERT(outstanding_, "LSU outstanding是空的，但是收到了响应");
    if (trans.is_response_error()) {
        SIM_ERROR(basename(), "lsu2dtcm响应错误");
    }

    if (trans.get_command() == tlm::TLM_READ_COMMAND) {
        const uint32_t lane = load_le32(outstanding_->data);
        const uint32_t half = (lane >> ((outstanding_->addr & 0x2) * 8)) & 0xffffu;
        const uint32_t byte = (lane >> ((outstanding_->addr & 0x3) * 8)) & 0xffu;
        SIM_INFO(basename(), "收到读响应, lane=0x" << std::hex << lane
                             << ", addr=0x" << outstanding_->addr
                             << ", extract_half=0x" << half
                             << ", extract_byte=0x" << byte << std::dec);
    } else {
        SIM_INFO(basename(), "收到写响应, addr=0x"
                             << std::hex << outstanding_->addr
                             << std::dec << ", 释放LSU outstanding");
    }

    outstanding_.reset();
    if (test_state_ != test_state::done) {
        if (test_state_ == test_state::read_after_store_byte && test_count_ != 0) {
            test_count_--;
            test_state_ = test_state::store_word;
            test_addr_ = 0x80000000;
        } else {
            test_state_ = static_cast<test_state>(static_cast<int>(test_state_) + 1);
        }
    }
    resp_event_.notify(SC_ZERO_TIME);
}

void lsu_ca::send_test_dtcm(std::unique_ptr<trans_ctx> ctx)
{
    SIM_ASSERT(!outstanding_, "LSU只允许一个全局outstanding事务");
    tlm::tlm_phase phase(tlm::BEGIN_REQ);
    sc_time delay(SC_ZERO_TIME);
    outstanding_ = std::move(ctx);
    const tlm::tlm_sync_enum res =
        lsu2dtcm_initiator_socket->nb_transport_fw(outstanding_->trans, phase, delay);
    SIM_INFO(basename(), "lsu 收到fw返回: sync=" << tlm_sync_name(res)
                         << ", phase=" << tlm_phase_name(phase)
                         << ", delay=" << delay);
}

void lsu_ca::send_test_itcm(std::unique_ptr<trans_ctx> ctx)
{
    SIM_ASSERT(!outstanding_, "LSU只允许一个全局outstanding事务");
    tlm::tlm_phase phase(tlm::BEGIN_REQ);
    sc_time delay(SC_ZERO_TIME);
    outstanding_ = std::move(ctx);
    const tlm::tlm_sync_enum res =
        lsu2itcm_initiator_socket->nb_transport_fw(outstanding_->trans, phase, delay);
    SIM_INFO(basename(), "lsu 收到fw返回: sync=" << tlm_sync_name(res)
                         << ", phase=" << tlm_phase_name(phase)
                         << ", delay=" << delay);
}

void lsu_ca::send_test_biu(std::unique_ptr<trans_ctx> ctx)
{
    SIM_ASSERT(!outstanding_, "LSU只允许一个全局outstanding事务");
    tlm::tlm_phase phase(tlm::BEGIN_REQ);
    sc_time delay(SC_ZERO_TIME);
    outstanding_ = std::move(ctx);
    const tlm::tlm_sync_enum res =
        lsu2biu_initiator_socket->nb_transport_fw(outstanding_->trans, phase, delay);
    SIM_INFO(basename(), "lsu 收到fw返回: sync=" << tlm_sync_name(res)
                         << ", phase=" << tlm_phase_name(phase)
                         << ", delay=" << delay);
}

std::unique_ptr<lsu_ca::trans_ctx> lsu_ca::make_store_word(uint32_t addr, uint32_t value)
{
    auto ctx = std::make_unique<trans_ctx>();
    ctx->addr = addr;
    ctx->size = 2;
    store_le32(ctx->data, value);
    ctx->byte_enable = {1, 1, 1, 1};
    ctx->trans.set_command(tlm::TLM_WRITE_COMMAND);
    ctx->trans.set_address(addr);
    ctx->trans.set_data_ptr(ctx->data.data());
    ctx->trans.set_data_length(4);
    ctx->trans.set_byte_enable_ptr(ctx->byte_enable.data());
    ctx->trans.set_byte_enable_length(4);
    ctx->trans.set_streaming_width(4);
    ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return ctx;
}

std::unique_ptr<lsu_ca::trans_ctx> lsu_ca::make_store_half(uint32_t addr, uint16_t value)
{
    auto ctx = std::make_unique<trans_ctx>();
    ctx->addr = addr;
    ctx->size = 1;

    const uint32_t wdata = (static_cast<uint32_t>(value) << 16)
                         | static_cast<uint32_t>(value);
    store_le32(ctx->data, wdata);

    const uint32_t off = addr & 0x2;
    ctx->byte_enable = (off == 0) ? std::array<uint8_t, 4>{1, 1, 0, 0}
                                  : std::array<uint8_t, 4>{0, 0, 1, 1};

    ctx->trans.set_command(tlm::TLM_WRITE_COMMAND);
    ctx->trans.set_address(addr);
    ctx->trans.set_data_ptr(ctx->data.data());
    ctx->trans.set_data_length(4);
    ctx->trans.set_byte_enable_ptr(ctx->byte_enable.data());
    ctx->trans.set_byte_enable_length(4);
    ctx->trans.set_streaming_width(4);
    ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return ctx;
}

std::unique_ptr<lsu_ca::trans_ctx> lsu_ca::make_store_byte(uint32_t addr, uint8_t value)
{
    auto ctx = std::make_unique<trans_ctx>();
    ctx->addr = addr;
    ctx->size = 0;

    const uint32_t wdata = static_cast<uint32_t>(value) * 0x01010101u;
    store_le32(ctx->data, wdata);
    ctx->byte_enable = {0, 0, 0, 0};
    ctx->byte_enable[addr & 0x3] = 1;

    ctx->trans.set_command(tlm::TLM_WRITE_COMMAND);
    ctx->trans.set_address(addr);
    ctx->trans.set_data_ptr(ctx->data.data());
    ctx->trans.set_data_length(4);
    ctx->trans.set_byte_enable_ptr(ctx->byte_enable.data());
    ctx->trans.set_byte_enable_length(4);
    ctx->trans.set_streaming_width(4);
    ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return ctx;
}

std::unique_ptr<lsu_ca::trans_ctx> lsu_ca::make_load_word(uint32_t addr)
{
    auto ctx = std::make_unique<trans_ctx>();
    ctx->addr = addr;
    ctx->size = 2;
    ctx->trans.set_command(tlm::TLM_READ_COMMAND);
    ctx->trans.set_address(addr);
    ctx->trans.set_data_ptr(ctx->data.data());
    ctx->trans.set_data_length(4);
    ctx->trans.set_streaming_width(4);
    ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return ctx;
}

void lsu_ca::issue_next_test()
{
    int hit = memories_.size()-1;
    for(std::size_t i = 0; i < memories_.size(); i++) {
        if(test_addr_ >= memories_[i]->base_addr && test_addr_ < memories_[i]->base_addr + memories_[i]->size) {
            hit = i;
        }}
    auto send_to_hit = [this, hit](std::unique_ptr<trans_ctx> ctx) {
        if (hit == 0) {
            send_test_dtcm(std::move(ctx));
        } else if (hit == 1) {
            send_test_itcm(std::move(ctx));
        } else {
            send_test_biu(std::move(ctx));
        }
    };
    switch (test_state_) {
    case test_state::store_word:
        SIM_INFO(basename(), "测试1: store word addr=0x" << std::hex
                             << test_addr_ << ", value=0x11223344" << std::dec);
        send_to_hit(make_store_word(test_addr_, 0x11223344));
        break;
    case test_state::read_after_store_word:
        SIM_INFO(basename(), "测试2: load word检查整lane addr=0x" << std::hex
                             << test_addr_ << std::dec);
        send_to_hit(make_load_word(test_addr_));
        break;
    case test_state::store_half_high:
        SIM_INFO(basename(), "测试3: store halfword高16位 addr=0x" << std::hex
                             << (test_addr_ + 2) << ", value=0xaabb" << std::dec);
        send_to_hit(make_store_half(test_addr_ + 2, 0xaabb));
        break;
    case test_state::read_after_store_half:
        SIM_INFO(basename(), "测试4: load word检查halfword写入后的lane");
        send_to_hit(make_load_word(test_addr_));
        break;
    case test_state::store_byte_mid:
        SIM_INFO(basename(), "测试5: store byte中间字节 addr=0x" << std::hex
                             << (test_addr_ + 1) << ", value=0xcc" << std::dec);
        send_to_hit(make_store_byte(test_addr_ + 1, 0xcc));
        break;
    case test_state::read_after_store_byte:
        SIM_INFO(basename(), "测试6: load word检查byte写入后的lane");
        send_to_hit(make_load_word(test_addr_));
        break;
    case test_state::done:
        if (!done_logged_) {
            SIM_INFO(basename(), "LSU读写测试序列完成");
            done_logged_ = true;
        }
        break;
    }
}

void lsu_ca::thread()
{
    while (true) {
        wait(clk_period_, resp_event_);
        // if (!outstanding_) {
        //     issue_next_test();
        // } else {
        //     SIM_INFO(basename(), "LSU主线程检测到仍有outstanding，等待响应");
        // }
    }
}
