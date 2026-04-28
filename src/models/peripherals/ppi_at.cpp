#include "models/peripherals/ppi_at.h"
#include "common/debug_logger.h"
#include <iostream>

using namespace std;
using namespace sc_core;

ppi_at::ppi_at(sc_module_name module_name)
    : sc_module(module_name)
{
    router2ppi_target_socket.register_b_transport(this, &ppi_at::b_transport);
    E203_DEBUG_STREAM(module_name << " created !");
}

ppi_at::~ppi_at() {}

void ppi_at::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    (void)delay;
    E203_DEBUG_STREAM("[" << sc_time_stamp() << "] "
                          << name()
                          << " b_transport addr=0x" << hex << trans.get_address() << dec);

    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}
