#include "models/bus/router_at.h" 
#include "common/debug_logger.h"
#include <iostream>
using namespace std;
using namespace sc_core;

router_at::router_at(sc_module_name module_name)
    :sc_module(module_name)
{
    cpu2biu_target_socket.register_b_transport(this, &router_at::b_transport);
    E203_DEBUG_STREAM(module_name << " created !");
}

router_at::~router_at(){}

void router_at::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    const sc_dt::uint64 addr = trans.get_address();
    E203_DEBUG_STREAM("[" << sc_time_stamp() << "] "
                          << name()
                          << " b_transport addr=0x" << hex << addr << dec);

    if (addr >= 0x02000000 && addr < 0x02010000) {
        router2clint_initiator_socket->b_transport(trans, delay);
    } else if (addr >= 0x0C000000 && addr < 0x0D000000) {
        router2plic_initiator_socket->b_transport(trans, delay);
    } else if (addr >= 0x10000000 && addr < 0x20000000) {
        router2ppi_initiator_socket->b_transport(trans, delay);
    } else if (addr >= 0xF0000000 && addr <= 0xFFFFFFFF) {
        router2fio_initiator_socket->b_transport(trans, delay);
    } else {
        router2mem_initiator_socket->b_transport(trans, delay);
    }
}
