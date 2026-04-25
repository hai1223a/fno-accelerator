#include "models/nice/nice_at.h"
#include <iostream>

using namespace std;
using namespace sc_core;

nice_at::nice_at(sc_module_name module_name)
    : sc_module(module_name)
{
    cpu2nice_target_socket.register_b_transport(this, &nice_at::b_transport);
    cout << module_name << " created !" << endl;
}

nice_at::~nice_at() {}

void nice_at::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    (void)delay;
    cout << "[" << sc_time_stamp() << "] "
         << name()
         << " b_transport addr=0x" << hex << trans.get_address() << dec
         << endl;

    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}
