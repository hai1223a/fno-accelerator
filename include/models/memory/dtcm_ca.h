#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <queue>
#include "models/memory/sram_ca.h"
#include "common/types.h"

class dtcm_ca : public sc_core::sc_module, public sram_ca
{
private:
    e203sim::memory_config config;
    sc_core::sc_time clk_period;
    tlm::tlm_generic_payload* trans_pending;
    sc_core::sc_event resp_event;
    void access_memory(tlm::tlm_generic_payload& trans);
    void resp_thread();
public:
    SC_HAS_PROCESS(dtcm_ca);
    tlm_utils::simple_target_socket<dtcm_ca> lsu2dtcm_target_socket;
    tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase, sc_core::sc_time& delay);
    dtcm_ca(sc_core::sc_module_name module_name, e203sim::memory_config* cfg, uint32_t cycle_unit);
    ~dtcm_ca();
};
