#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

class ppi_at : public sc_core::sc_module
{
private:

public:
    tlm_utils::simple_target_socket<ppi_at> router2ppi_target_socket;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    ppi_at(sc_core::sc_module_name module_name);
    ~ppi_at();
};
