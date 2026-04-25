#include "models/core/core_at.h"
#include <iostream>
using namespace std;
using namespace sc_core;

core_at::core_at(sc_module_name module_name)
    :sc_module(module_name)
{
    SC_THREAD(start);
    cout << module_name << " created !" << endl;
}

core_at::~core_at(){}

void core_at::send_test(tlm_utils::simple_initiator_socket<core_at>& socket, sc_dt::uint64 addr, const char* port_name)
{
    tlm::tlm_generic_payload trans;
    sc_time delay = sc_time(10, SC_NS);
    trans.set_address(addr);
    cout << "[" << sc_time_stamp() << "] "
             << "core_at send read via " << port_name
             << " addr=0x" << hex << addr << dec
             << endl;
    socket->b_transport(trans, delay);
    wait(delay);
}
void core_at::start()
{
    send_test(core2itlm_initiator_socket, 0x80000000, "itlm");
    send_test(core2dtlm_initiator_socket, 0x90000000, "dtlm");
    send_test(core2biu_initiator_socket, 0x10000000, "biu");
    send_test(core2nice_initiator_socket, 0x00000000, "nice");
    sc_stop();
}
