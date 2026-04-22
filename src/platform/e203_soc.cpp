#include "platform/e203_soc.h" 
#include "models/bus/router_at.h"
#include "models/core/cpu_at.h"
#include "models/peripherals/clint_at.h"
#include "models/peripherals/mem_at.h"
#include "models/peripherals/ppi_at.h"
#include <iostream>

using namespace std;
using namespace sc_core;

e203_soc::e203_soc(sc_module_name module_name)
    :sc_module(module_name)
{
    cpu_at_ = new cpu_at("cpu");
    router_at_ = new router_at("bus");
    clint_at_ = new clint_at("clint");
    mem_at_ = new mem_at("sys_mem");
    ppi_at_ = new ppi_at("sys_ppi");

    cpu_at_->cpu2biu_initiator_socket.bind(router_at_->cpu2biu_target_socket);

    cout << module_name << " created !" << endl;
}

e203_soc::~e203_soc()
{
    delete ppi_at_;
    delete mem_at_;
    delete clint_at_;
    delete router_at_;
    delete cpu_at_;
}
