#include "models/core/ifu_ca.h"
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

uint64_t load_le64(const std::array<uint8_t, 8>& data)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        value |= static_cast<uint64_t>(data[i]) << (i * 8);
    }
    return value;
}

} // namespace

ifu_ca::ifu_ca(sc_module_name module_name, const e203sim::sim_config& config)
    : sc_module(module_name)
{
    (void)config;
    ifu2itcm_initiator_socket.register_nb_transport_bw(this, &ifu_ca::nb_transport_bw);
    INFO(module_name << " created !");
}

ifu_ca::~ifu_ca() = default;

tlm::tlm_sync_enum ifu_ca::nb_transport_bw(tlm::tlm_generic_payload& trans,
                                           tlm::tlm_phase& phase,
                                           sc_core::sc_time& delay)
{
    (void)delay;
    if (phase == tlm::BEGIN_RESP) {
        SIM_INFO(basename(), "ifu收到BEGIN_RESP, addr=0x"
                             << std::hex << trans.get_address() << std::dec);
        phase = tlm::END_RESP;
        handle_response(trans);
        SIM_INFO(basename(), "ifu返回END_RESP，事务完成");
        return tlm::TLM_COMPLETED;
    }
    return tlm::TLM_ACCEPTED;
}

void ifu_ca::issue_read32(uint32_t addr)
{
    send_read32(make_read32(addr));
}

void ifu_ca::send_read32(std::unique_ptr<trans_ctx> ctx)
{
    SIM_ASSERT(!outstanding_, "IFU只允许一个outstanding取指事务");

    tlm::tlm_phase phase(tlm::BEGIN_REQ);
    sc_time delay(SC_ZERO_TIME);
    outstanding_ = std::move(ctx);

    SIM_INFO(basename(), "ifu发起BEGIN_REQ: addr=0x"
                         << std::hex << outstanding_->addr << std::dec
                         << ", read32 over 64-bit ITCM lane");

    const tlm::tlm_sync_enum res =
        ifu2itcm_initiator_socket->nb_transport_fw(outstanding_->trans, phase, delay);
    SIM_INFO(basename(), "ifu收到fw返回: sync="
                         << tlm_sync_name(res)
                         << ", phase=" << tlm_phase_name(phase)
                         << ", delay=" << delay);
}

void ifu_ca::handle_response(tlm::tlm_generic_payload& trans)
{
    SIM_ASSERT(outstanding_, "IFU outstanding是空的，但是收到了响应");
    if (trans.is_response_error()) {
        SIM_ERROR(basename(), "ifu2itcm响应错误");
    }

    const uint64_t lane = load_le64(outstanding_->data);
    SIM_INFO(basename(), "收到取指响应, lane64=0x" << std::hex << lane
                         << ", addr=0x" << outstanding_->addr << std::dec);
    outstanding_.reset();
}

std::unique_ptr<ifu_ca::trans_ctx> ifu_ca::make_read32(uint32_t addr)
{
    auto ctx = std::make_unique<trans_ctx>();
    ctx->addr = addr;
    ctx->trans.set_command(tlm::TLM_READ_COMMAND);
    ctx->trans.set_address(addr);
    ctx->trans.set_data_ptr(ctx->data.data());
    ctx->trans.set_data_length(8);
    ctx->trans.set_streaming_width(8);
    ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return ctx;
}
