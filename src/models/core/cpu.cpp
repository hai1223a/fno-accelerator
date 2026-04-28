#include "models/core/cpu.h"
#include "common/debug_logger.h"
#include "models/core/core_ca.h"
#include "models/memory/memory_ca.h"
#include <iostream>

using namespace std;
using namespace sc_core;

cpu::cpu(sc_module_name module_name)
    : sc_module(module_name)
{
    core_ca_ = new core_ca("core");
    itlm_ca_ = new memory_ca("itlm");
    dtlm_ca_ = new memory_ca("dtlm");

    core_ca_->core2itlm_initiator_socket.bind(itlm_ca_->core2tlm_target_socket);
    core_ca_->core2dtlm_initiator_socket.bind(dtlm_ca_->core2tlm_target_socket);
    core_ca_->core2biu_initiator_socket.bind(cpu2biu_initiator_socket);
    core_ca_->core2nice_initiator_socket.bind(cpu2nice_initiator_socket);

    E203_DEBUG_STREAM(module_name << " created !");
}

cpu::~cpu()
{
    delete dtlm_ca_;
    delete itlm_ca_;
    delete core_ca_;
}
