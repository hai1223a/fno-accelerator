#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

class core_at: public sc_core::sc_module
{
private:
    void start();
public:
    SC_HAS_PROCESS(core_at);
    tlm_utils::simple_initiator_socket<core_at> core2itlm_initiator_socket;
    tlm_utils::simple_initiator_socket<core_at> core2dtlm_initiator_socket;
    tlm_utils::simple_initiator_socket<core_at> core2biu_initiator_socket;
    core_at(sc_core::sc_module_name module_name);
    ~core_at();
};
