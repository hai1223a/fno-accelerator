#include "models/memory/memory_ca.h"
#include "common/debug_logger.h"
#include <iostream>

using namespace std;
using namespace sc_core;

memory_ca::memory_ca(sc_module_name module_name)
    : sc_module(module_name)
{
    core2tlm_target_socket.register_b_transport(this, &memory_ca::b_transport);
    E203_DEBUG_STREAM(module_name << " created !");
}

memory_ca::~memory_ca() {}

void memory_ca::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    (void)trans;
    (void)delay;
    E203_DEBUG_STREAM("[" << sc_time_stamp() << "] "
                          << name()
                          << " b_transport addr=0x" << hex << trans.get_address() << dec);

    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}
