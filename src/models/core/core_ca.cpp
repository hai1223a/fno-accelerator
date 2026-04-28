#include "models/core/core_ca.h"
#include "common/debug_logger.h"
#include <iostream>
using namespace std;
using namespace sc_core;

core_ca::core_ca(sc_module_name module_name)
    :sc_module(module_name)
{
    SC_THREAD(start);
    E203_DEBUG_STREAM(module_name << " created !");
}

core_ca::~core_ca(){}

void core_ca::send_test(tlm_utils::simple_initiator_socket<core_ca>& socket, sc_dt::uint64 addr, const char* port_name)
{
    tlm::tlm_generic_payload trans;
    sc_time delay = sc_time(10, SC_NS);
    trans.set_address(addr);
    E203_DEBUG_STREAM("[" << sc_time_stamp() << "] "
                           << "core_ca send read via " << port_name
                           << " addr=0x" << hex << addr << dec);
    socket->b_transport(trans, delay);
    wait(delay);
}
void core_ca::start()
{
    send_test(core2itlm_initiator_socket, 0x80000000, "itlm");
    send_test(core2dtlm_initiator_socket, 0x90000000, "dtlm");
    send_test(core2biu_initiator_socket, 0x10000000, "biu");
    send_test(core2nice_initiator_socket, 0x00000000, "nice");
    sc_stop();
}
