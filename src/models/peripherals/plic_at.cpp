#include "models/peripherals/plic_at.h"
#include <iostream>

using namespace std;
using namespace sc_core;

plic_at::plic_at(sc_module_name module_name)
    : sc_module(module_name)
{
    router2plic_target_socket.register_b_transport(this, &plic_at::b_transport);
    cout << module_name << " created !" << endl;
}

plic_at::~plic_at() {}

void plic_at::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    (void)delay;
    cout << "[" << sc_time_stamp() << "] "
         << name()
         << " b_transport addr=0x" << hex << trans.get_address() << dec
         << endl;

    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}
