#include "models/core/cpu.h"
#include "models/core/core_at.h"
#include "models/memory/memory_at.h"
#include <iostream>

using namespace std;
using namespace sc_core;

cpu::cpu(sc_module_name module_name)
    : sc_module(module_name)
{
    core_at_ = new core_at("core");
    itlm_at_ = new memory_at("itlm");
    dtlm_at_ = new memory_at("dtlm");

    core_at_->core2itlm_initiator_socket.bind(itlm_at_->core2tlm_target_socket);
    core_at_->core2dtlm_initiator_socket.bind(dtlm_at_->core2tlm_target_socket);
    core_at_->core2biu_initiator_socket.bind(cpu2biu_initiator_socket);
    core_at_->core2nice_initiator_socket.bind(cpu2nice_initiator_socket);

    cout << module_name << " created !" << endl;
}

cpu::~cpu()
{
    delete dtlm_at_;
    delete itlm_at_;
    delete core_at_;
}
