#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "common/types.h"

class core_ca;
class dtcm_ca;
class itcm_ca;
class cpu : public sc_core::sc_module
{
private:
    core_ca *core_ca_;
    itcm_ca *itcm_ca_;
    dtcm_ca *dtcm_ca_;
public:
    tlm::tlm_initiator_socket<> cpu2biu_initiator_socket;
    tlm::tlm_initiator_socket<> cpu2nice_initiator_socket;

    cpu(sc_core::sc_module_name module_name, const e203sim::sim_config &config);
    ~cpu();
};
