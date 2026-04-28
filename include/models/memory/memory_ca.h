#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

class memory_ca : public sc_core::sc_module
{
private:
public:
    tlm_utils::simple_target_socket<memory_ca> core2tlm_target_socket;
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    memory_ca(sc_core::sc_module_name module_name);
    ~memory_ca();
};
