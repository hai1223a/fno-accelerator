#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

class core_ca: public sc_core::sc_module
{
private:
    void start();
    void send_test(tlm_utils::simple_initiator_socket<core_ca>& socket, sc_dt::uint64 addr, const char* port_name);
public:
    SC_HAS_PROCESS(core_ca);
    tlm_utils::simple_initiator_socket<core_ca> core2itlm_initiator_socket;
    tlm_utils::simple_initiator_socket<core_ca> core2dtlm_initiator_socket;
    tlm_utils::simple_initiator_socket<core_ca> core2biu_initiator_socket;
    tlm_utils::simple_initiator_socket<core_ca> core2nice_initiator_socket;
    core_ca(sc_core::sc_module_name module_name);
    ~core_ca();
};
