#include "models/memory/memory_at.h"
#include <iostream>

using namespace std;
using namespace sc_core;

memory_at::memory_at(sc_module_name module_name)
    : sc_module(module_name)
{
    core2tlm_target_socket.register_b_transport(this, &memory_at::b_transport);
    cout << module_name << " created !" << endl;
}

memory_at::~memory_at() {}

void memory_at::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    (void)trans;
    (void)delay;
    std::cout << "[" << sc_core::sc_time_stamp() << "] "
              << name()
              << " b_transport"
              << std::endl;
}
