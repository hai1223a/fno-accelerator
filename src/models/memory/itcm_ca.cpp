#include "models/memory/itcm_ca.h"
#include <iostream>
#include "common/debug_logger.h"

using namespace sc_core;

namespace {

uint64_t pack_le(const uint8_t* data, unsigned int len, unsigned int lane_offset)
{
    uint64_t value = 0;
    for (unsigned int i = 0; i < len; ++i) {
        value |= static_cast<uint64_t>(data[i]) << ((lane_offset + i) * 8);
    }
    return value;
}

void unpack_le(uint64_t value, uint8_t* data, unsigned int len, unsigned int lane_offset)
{
    for (unsigned int i = 0; i < len; ++i) {
        data[i] = static_cast<uint8_t>(value >> ((lane_offset + i) * 8));
    }
}

uint8_t make_strobe(const tlm::tlm_generic_payload& trans,
                    unsigned int transfer_len,
                    unsigned int lane_offset)
{
    const unsigned int be_len = trans.get_byte_enable_length();
    const auto* be_ptr = trans.get_byte_enable_ptr();
    SIM_ASSERT(be_len == 0 || be_ptr != nullptr, "你提供了不正确的字节掩码");

    uint8_t strobe = 0;
    if (be_len == 0) {
        for (unsigned int i = 0; i < transfer_len; ++i) {
            strobe |= static_cast<uint8_t>(1u << (lane_offset + i));
        }
        return strobe;
    }

    SIM_ASSERT(be_len <= transfer_len, "itcm byte enable length must not exceed transfer length");
    for (unsigned int i = 0; i < be_len; ++i) {
        if (be_ptr[i] != 0) {
            strobe |= static_cast<uint8_t>(1u << (lane_offset + i));
        }
    }
    return strobe;
}

} // namespace

itcm_ca::itcm_ca(sc_module_name module_name, e203sim::memory_config* cfg, uint32_t cycle_unit)
    : sc_module(module_name), sram_ca(64, cfg->size), config(*cfg),
      clk_period(cycle_unit, SC_NS)
{
    lsu2itcm_target_socket.register_nb_transport_fw(this, &itcm_ca::nb_transport_fw_lsu);
    ifu2itcm_target_socket.register_nb_transport_fw(this, &itcm_ca::nb_transport_fw_ifu);
    INFO(module_name << " created !");
    SC_THREAD(resp_thread);
}

itcm_ca::~itcm_ca() {}

tlm::tlm_sync_enum itcm_ca::nb_transport_fw_lsu(tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase, sc_time& delay)
{
    return accept_request(trans, phase, delay, trans_source::lsu);
}

tlm::tlm_sync_enum itcm_ca::nb_transport_fw_ifu(tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase, sc_time& delay)
{
    return accept_request(trans, phase, delay, trans_source::ifu);
}

tlm::tlm_sync_enum itcm_ca::accept_request(tlm::tlm_generic_payload& trans,
                                           tlm::tlm_phase& phase,
                                           sc_time& delay,
                                           trans_source source)
{
    if (phase == tlm::BEGIN_REQ) {
        if (source_busy(source)) {
            SIM_INFO(basename(), "ITCM通路忙，暂不接收BEGIN_REQ: src="
                                 << ((source == trans_source::ifu) ? "IFU" : "LSU"));
            return tlm::TLM_ACCEPTED;
        }

        pending_slot(source).trans = &trans;
        dispatch_event_.notify(delay);
        phase = tlm::END_REQ;
        delay = SC_ZERO_TIME;

        return tlm::TLM_UPDATED;
    }

    if (phase == tlm::END_RESP) {
        return tlm::TLM_COMPLETED;
    }

    return tlm::TLM_ACCEPTED;
}

bool itcm_ca::source_busy(trans_source source) const
{
    if (source == trans_source::lsu) {
        return lsu_pending_.trans != nullptr || lsu_active_;
    }
    return ifu_pending_.trans != nullptr || ifu_active_;
}

bool itcm_ca::has_pending_request() const
{
    return lsu_pending_.trans != nullptr || ifu_pending_.trans != nullptr;
}

itcm_ca::pending_request& itcm_ca::pending_slot(trans_source source)
{
    return (source == trans_source::lsu) ? lsu_pending_ : ifu_pending_;
}

itcm_ca::trans_source itcm_ca::select_next_source() const
{
    if (lsu_pending_.trans != nullptr) {
        return trans_source::lsu;
    }
    return trans_source::ifu;
}

void itcm_ca::set_active(trans_source source, bool active)
{
    if (source == trans_source::lsu) {
        lsu_active_ = active;
    } else {
        ifu_active_ = active;
    }
}

void itcm_ca::resp_thread()
{
    while (true) {
        wait(dispatch_event_);
        while (has_pending_request()) {
            const trans_source source = select_next_source();
            auto& slot = pending_slot(source);
            auto* trans_pending = slot.trans;
            slot.trans = nullptr;
            set_active(source, true);

            wait(clk_period);
            access_memory(*trans_pending, source);

            tlm::tlm_phase phase = tlm::BEGIN_RESP;
            sc_time delay = SC_ZERO_TIME;

            SIM_INFO(basename(), "ITCM发起BEGIN_RESP: addr=0x"
                                 << std::hex << trans_pending->get_address()
                                 << std::dec );
            if (source == trans_source::ifu) {
                ifu2itcm_target_socket->nb_transport_bw(*trans_pending, phase, delay);
            } else {
                lsu2itcm_target_socket->nb_transport_bw(*trans_pending, phase, delay);
            }
            set_active(source, false);
        }
    }
}

void itcm_ca::access_memory(tlm::tlm_generic_payload& trans, trans_source source)
{
    const auto cmd = trans.get_command();
    const auto addr = trans.get_address();
    auto* data = trans.get_data_ptr();

    if (addr >= config.base_addr + config.size || addr < config.base_addr) {
        SIM_INFO(basename(), "ITCM地址越界: addr=0x" << std::hex << addr
                             << ", base=0x" << config.base_addr
                             << ", size=0x" << config.size << std::dec);
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    SIM_ASSERT(data != nullptr, "ITCM payload data pointer must not be null");

    const unsigned int data_len = trans.get_data_length();
    const unsigned int transfer_len = (source == trans_source::lsu)
                                          ? ((data_len < 4) ? data_len : 4)
                                          : ((data_len < 8) ? data_len : 8);
    const unsigned int lane_offset = (source == trans_source::lsu) ? static_cast<unsigned int>(addr & 0x4u) : 0u;

    if (cmd == tlm::TLM_WRITE_COMMAND) {
        const uint8_t strobe = make_strobe(trans, transfer_len, lane_offset);
        const uint64_t value = pack_le(data, transfer_len, lane_offset);
        SIM_INFO(basename(), "访问SRAM写: addr=0x" << std::hex << addr
                             << ", value=0x" << value
                             << ", strobe=" << static_cast<uint32_t>(strobe) << std::dec);
        if (!sram_write(addr - config.base_addr, value, strobe)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
    } else if (cmd == tlm::TLM_READ_COMMAND) {
        uint64_t value = 0;
        if (!sram_read(addr - config.base_addr, value)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        SIM_INFO(basename(), "访问SRAM读: addr=0x" << std::hex << addr
                             << ", value=0x" << value << std::dec);
        unpack_le(value, data, transfer_len, lane_offset);
    } else {
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}
