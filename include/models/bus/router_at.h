#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

class router_at: public sc_core::sc_module
{
private:
    
public:
    tlm_utils::simple_target_socket<router_at> cpu2biu_target_socket;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    router_at(sc_core::sc_module_name module_name);
    ~router_at();
};
