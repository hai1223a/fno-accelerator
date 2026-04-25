#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

class nice_at : public sc_core::sc_module
{
private:

public:
    tlm_utils::simple_target_socket<nice_at> cpu2nice_target_socket;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    nice_at(sc_core::sc_module_name module_name);
    ~nice_at();
};
