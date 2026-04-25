#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

class fio_at : public sc_core::sc_module
{
private:

public:
    tlm_utils::simple_target_socket<fio_at> router2fio_target_socket;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    fio_at(sc_core::sc_module_name module_name);
    ~fio_at();
};
