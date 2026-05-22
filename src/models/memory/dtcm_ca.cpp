#include "models/memory/dtcm_ca.h"
#include "common/debug_logger.h"
#include "sim/thread_trace.h"
#include <iostream>

using namespace sc_core;

dtcm_ca::dtcm_ca(sc_module_name module_name, e203sim::memory_config* cfg, uint32_t cycle_unit)
    : sc_module(module_name), sram_ca(32, cfg->size), config(*cfg),
      clk_period(cycle_unit, SC_NS), trans_pending(nullptr)
{
    lsu2dtcm_target_socket.register_nb_transport_fw(this, &dtcm_ca::nb_transport_fw);
    INFO(module_name << " created !");
    SC_THREAD(resp_thread);
}

dtcm_ca::~dtcm_ca() {}

tlm::tlm_sync_enum dtcm_ca::nb_transport_fw(tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase, sc_time& delay)
{
    // DTCM 是 single-slot non-blocking target：BEGIN_REQ 接收后按一个 cycle 返回 BEGIN_RESP。
    return e203sim::accept_nb_begin_req(trans_pending,
                                        trans,
                                        phase,
                                        delay,
                                        resp_event,
                                        delay + clk_period,
                                        basename());
}

void dtcm_ca::resp_thread()
{
    while (true)
    {
        wait(resp_event);
        THREAD_TRACE("DTCM响应线程", "线程唤醒", "有待处理请求=" << (trans_pending != nullptr));
        while (trans_pending)
        {
            THREAD_TRACE("DTCM响应线程", "访问SRAM", "地址=0x" << std::hex
                         << trans_pending->get_address() << std::dec);
            // access_memory 在响应前完成实际 SRAM 读写并设置 response_status。
            access_memory(*trans_pending);

            tlm::tlm_phase phase = tlm::BEGIN_RESP;
            sc_time delay = SC_ZERO_TIME;

            THREAD_TRACE("DTCM响应线程", "返回响应", "地址=0x" << std::hex
                         << trans_pending->get_address() << std::dec);
            lsu2dtcm_target_socket->nb_transport_bw(*trans_pending, phase, delay);
            trans_pending = nullptr;
        }
    }
}

void dtcm_ca::access_memory(tlm::tlm_generic_payload& trans)
{
    auto cmd = trans.get_command();
    auto addr = trans.get_address();
    auto* data = trans.get_data_ptr();

    if (addr >= config.base_addr + config.size || addr < config.base_addr)
    {
        SIM_ERROR(basename(), "取值地址不在范围内， " << "addr: " << std::hex << addr);
    }
    if(cmd == tlm::TLM_WRITE_COMMAND)
    {
        // DTCM data path 以 32-bit lane 建模；sub-word store 通过 byte enable 控制。
        auto be_len = trans.get_byte_enable_length();
        auto* be_ptr = trans.get_byte_enable_ptr();
        SIM_ASSERT(be_len == 0 || be_ptr != nullptr, "你提供了不正确的字节掩码");
        SIM_ASSERT(be_len <= 4, "dtcm byte enable length must be <= 4");
        uint8_t strobe = 0;
        for (unsigned int i = 0; i < be_len; i++)
        {
            strobe |= be_ptr[i] << i;
        }
        uint64_t value = 0;
        for(int i = 0; i < 4; i++)
        {
            value |= static_cast<uint64_t>(data[i]) << (i * 8);
        }
        SIM_INFO(basename(), "访问SRAM写: addr=0x" << std::hex << addr
                             << ", value=0x" << value
                             << ", strobe=" << static_cast<uint32_t>(strobe) << std::dec);
        if(!sram_write(addr - config.base_addr, value, strobe))
        {
            SIM_ERROR(basename(), "写内存越界失败， " << "addr: " << std::hex << addr << "左边界： " << config.base_addr << "右边界： " << config.base_addr + config.size);
        }
    }
    else if(cmd == tlm::TLM_READ_COMMAND)
    {
        // 读响应只覆盖 payload 声明的 data_length，测试可检查短读不污染尾字节。
        uint64_t value = 0;
        if(!sram_read(addr - config.base_addr, value))
        {
            SIM_ERROR(basename(), "读内存越界失败， " << "addr: " << std::hex << addr << "左边界： " << config.base_addr << "右边界： " << config.base_addr + config.size);
        }
        SIM_INFO(basename(), "访问SRAM读: addr=0x" << std::hex << addr
                             << ", value=0x" << value << std::dec);
        const unsigned int data_len = trans.get_data_length();
        const unsigned int unpack_len = (data_len < 4) ? data_len : 4;
        for(unsigned int i = 0; i < unpack_len; i++)
        {
            data[i] = static_cast<uint8_t>(value >> (i * 8));
        }
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}
