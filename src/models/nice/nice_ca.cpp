#include "models/nice/nice_ca.h"
#include "common/debug_logger.h"
#include <iostream>

using namespace std;
using namespace sc_core;

nice_ca::nice_ca(sc_module_name module_name)
    : sc_module(module_name)
{
    cpu2nice_target_socket.register_b_transport(this, &nice_ca::b_transport);
    INFO(module_name << " created !");
}

nice_ca::~nice_ca() {}

void nice_ca::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    (void)delay;
    INFO("[" << sc_time_stamp() << "] "
                          << name()
                          << " b_transport addr=0x" << hex << trans.get_address() << dec);

    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}
