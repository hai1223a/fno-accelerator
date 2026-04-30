#pragma once

#include <cstdint>
#include <memory>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "common/types.h"

class lsu_ca;
class ifu_ca;

class core_ca: public sc_core::sc_module
{
private:
    lsu_ca* lsu_;
    ifu_ca* ifu_;

public:
    SC_HAS_PROCESS(core_ca);
    tlm_utils::simple_initiator_socket<core_ca> core2nice_initiator_socket;
    core_ca(sc_core::sc_module_name module_name, const e203sim::sim_config &config);
    ~core_ca();

    tlm::tlm_initiator_socket<> core2dtcm_initiator_socket;
    tlm::tlm_initiator_socket<64> corelsu2itcm_initiator_socket;
    tlm::tlm_initiator_socket<64> coreifu2itcm_initiator_socket;
    tlm::tlm_initiator_socket<> core2biu_initiator_socket;
    // tlm::tlm_initiator_socket<> core_lsu2itcm_initiator_socket;
};
