#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

class router_at: public sc_core::sc_module
{
private:
    
public:
    tlm_utils::simple_target_socket<router_at> cpu2biu_target_socket;
    tlm_utils::simple_initiator_socket<router_at> router2clint_initiator_socket;
    tlm_utils::simple_initiator_socket<router_at> router2plic_initiator_socket;
    tlm_utils::simple_initiator_socket<router_at> router2mem_initiator_socket;
    tlm_utils::simple_initiator_socket<router_at> router2ppi_initiator_socket;
    tlm_utils::simple_initiator_socket<router_at> router2fio_initiator_socket;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    router_at(sc_core::sc_module_name module_name);
    ~router_at();
};
