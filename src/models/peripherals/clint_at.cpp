#include "models/peripherals/clint_at.h"
#include "common/debug_logger.h"
#include <iostream>

using namespace std;
using namespace sc_core;

clint_at::clint_at(sc_module_name module_name)
    : sc_module(module_name)
{
    router2clint_target_socket.register_b_transport(this, &clint_at::b_transport);
    INFO(module_name << " created !");
}

clint_at::~clint_at() {}

void clint_at::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    (void)delay;
    INFO("[" << sc_time_stamp() << "] "
                          << name()
                          << " b_transport addr=0x" << hex << trans.get_address() << dec);

    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}
