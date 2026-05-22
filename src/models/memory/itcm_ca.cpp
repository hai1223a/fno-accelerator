#include "models/memory/itcm_ca.h"
#include <iostream>
#include <algorithm>
#include "common/debug_logger.h"
#include "sim/thread_trace.h"

using namespace sc_core;

namespace {

uint64_t pack_le(const uint8_t* data, unsigned int len, unsigned int lane_offset)
{
    // 将 payload 小端字节打包进 64-bit ITCM lane 的指定偏移。
    uint64_t value = 0;
    for (unsigned int i = 0; i < len; ++i) {
        value |= static_cast<uint64_t>(data[i]) << ((lane_offset + i) * 8);
    }
    return value;
}

void unpack_le(uint64_t value, uint8_t* data, unsigned int len, unsigned int lane_offset)
{
    // 从 64-bit lane 指定偏移解包到 payload data buffer。
    for (unsigned int i = 0; i < len; ++i) {
        data[i] = static_cast<uint8_t>(value >> ((lane_offset + i) * 8));
    }
}

uint8_t make_strobe(const tlm::tlm_generic_payload& trans,
                    unsigned int transfer_len,
                    unsigned int lane_offset)
{
    // TLM byte enable 转换为 sram_ca 使用的 lane strobe。
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

bool itcm_ca::contains_range(e203sim::addr_t addr, std::size_t size) const
{
    return addr >= config.base_addr &&
           size <= config.size &&
           addr - config.base_addr <= config.size - size;
}

void itcm_ca::load_binary(e203sim::addr_t load_addr, const std::vector<uint8_t>& bytes)
{
    if (!contains_range(load_addr, bytes.size())) {
        SIM_ERROR(basename(), "binary image must be loaded into ITCM range: load_addr=0x"
                              << std::hex << load_addr
                              << ", size=0x" << bytes.size()
                              << ", itcm=[0x" << config.base_addr
                              << ",0x" << (config.base_addr + config.size) << ")" << std::dec);
    }

    std::size_t offset = 0;
    while (offset < bytes.size()) {
        uint64_t lane = 0;
        uint8_t strobe = 0;
        const std::size_t chunk = std::min<std::size_t>(8, bytes.size() - offset);
        for (std::size_t i = 0; i < chunk; ++i) {
            lane |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
            strobe |= static_cast<uint8_t>(1u << i);
        }

        const auto local_addr = static_cast<uint32_t>((load_addr - config.base_addr) + offset);
        if (!sram_write(local_addr, lane, strobe)) {
            SIM_ERROR(basename(), "failed to write binary image into ITCM at local addr=0x"
                                  << std::hex << local_addr << std::dec);
        }
        offset += chunk;
    }
}

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
        // ITCM 为 IFU/LSU 保留独立 pending slot，但每个来源一次只允许一个请求。
        if (source_busy(source)) {
            SIM_INFO(basename(), "ITCM通路忙，暂不接收BEGIN_REQ: src="
                                 << ((source == trans_source::ifu) ? "IFU" : "LSU"));
            return tlm::TLM_ACCEPTED;
        }

        return e203sim::accept_nb_begin_req(pending_slot(source).trans,
                                            trans,
                                            phase,
                                            delay,
                                            dispatch_event_,
                                            delay,
                                            basename());
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
    // LSU 优先级高于 IFU，避免数据侧访问被连续取指饿死。
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
        THREAD_TRACE("ITCM响应线程", "线程唤醒", "有待处理请求=" << has_pending_request());
        while (has_pending_request()) {
            const trans_source source = select_next_source();
            auto& slot = pending_slot(source);
            auto* trans_pending = slot.trans;
            slot.trans = nullptr;
            set_active(source, true);
            THREAD_TRACE("ITCM响应线程", "选择请求", "来源="
                         << (source == trans_source::ifu ? "IFU" : "LSU")
                         << " 地址=0x" << std::hex << trans_pending->get_address() << std::dec);

            // ITCM target 使用 cycle latency 建模，delay 作为调度 annotation 累加到 event。
            wait(clk_period);
            access_memory(*trans_pending, source);

            tlm::tlm_phase phase = tlm::BEGIN_RESP;
            sc_time delay = SC_ZERO_TIME;

            SIM_INFO(basename(), "ITCM发起BEGIN_RESP: addr=0x"
                                 << std::hex << trans_pending->get_address()
                                 << std::dec );
            THREAD_TRACE("ITCM响应线程", "返回响应", "来源="
                         << (source == trans_source::ifu ? "IFU" : "LSU")
                         << " 地址=0x" << std::hex << trans_pending->get_address() << std::dec);
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
    // IFU 以 64-bit lane 取指；LSU 访问 ITCM 时仍按 32-bit 数据 lane 建模。
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
