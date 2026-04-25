#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

class plic_at : public sc_core::sc_module
{
private:

public:
    tlm_utils::simple_target_socket<plic_at> router2plic_target_socket;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    plic_at(sc_core::sc_module_name module_name);
    ~plic_at();
};
