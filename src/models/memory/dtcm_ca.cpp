#include "models/memory/dtcm_ca.h"
#include "common/debug_logger.h"
#include <iostream>

using namespace std;
using namespace sc_core;

dtcm_ca::dtcm_ca(sc_module_name module_name, e203sim::memory_config config, uint32_t cycle_unit)
    : sc_module(module_name), config(config), sram_ca(config.size, 32)
{
    clk_period = sc_time(cycle_unit, SC_NS);
    // core2tlm_target_socket.register_b_transport(this, &memory_ca::b_transport);
    lsu2dtcm_target_socket.register_nb_transport_fw(this, &dtcm_ca::nb_transport_fw);
    E203_DEBUG_STREAM(module_name << " created !");

}

dtcm_ca::~dtcm_ca() {}

tlm::tlm_sync_enum dtcm_ca::nb_transport_fw(tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase, sc_time& delay)
{
    if (phase == tlm::BEGIN_REQ)
    {
        trans_queue.push(&trans);
        resp_event.notify(delay + clk_period);
        phase = tlm::END_REQ;
        delay = SC_ZERO_TIME;

        return tlm::TLM_UPDATED;
    }

    if (phase == tlm::END_RESP)
    {
        return tlm::TLM_COMPLETED;
    }
    // if (phase == tlm::)
    return tlm::TLM_ACCEPTED;
}

void dtcm_ca::resp_thread()
{
    while (true)
    {
        wait(resp_event);
        while (!trans_queue.empty())
        {
            tlm::tlm_generic_payload* trans = trans_queue.front();
            trans_queue.pop();

            access_memory(*trans);

            tlm::tlm_phase phase = tlm::BEGIN_RESP;
            sc_time delay = SC_ZERO_TIME;

            lsu2dtcm_target_socket->nb_transport_bw(*trans, phase, delay);

            // 每拍只返回一个 response
            if (!trans_queue.empty()) {
                wait(clk_period);
            }
        }
    }
}

void dtcm_ca::access_memory(tlm::tlm_generic_payload& trans)
{
    auto cmd = trans.get_command();
    auto addr = trans.get_address();
    auto data = trans.get_data_ptr();
    auto size = trans.get_data_length();

    if (addr >= config.base_addr + config.size || addr < config.base_addr)
    {
        throw "dtcm access out of range";
    }
    if(cmd == tlm::TLM_WRITE_COMMAND)
    {
        auto be_len = trans.get_byte_enable_length();
        auto* be_ptr = trans.get_byte_enable_ptr();
        uint8_t strobe = 0;
        for (int i = 0; i < be_len; i++)
        {

        }
    }
    else if(cmd == tlm::TLM_READ_COMMAND)
    {
        for (int i = 0; i < size; i++)
        {
            // data[i] = dtcm[addr + i];
        }
    }
}