#include "platform/e203_soc.h"
#include "models/nice/nice_at.h"
#include "models/bus/router_at.h"
#include "models/core/cpu.h"
#include "models/peripherals/clint_at.h"
#include "models/peripherals/plic_at.h"
#include "models/peripherals/mem_at.h"
#include "models/peripherals/ppi_at.h"
#include "models/peripherals/fio_at.h"
#include <iostream>

using namespace std;
using namespace sc_core;

e203_soc::e203_soc(sc_module_name module_name)
    :sc_module(module_name)
{
    cpu_ = new cpu("cpu");
    router_at_ = new router_at("bus");
    nice_at_ = new nice_at("nice");
    clint_at_ = new clint_at("clint");
    plic_at_ = new plic_at("plic");
    mem_at_ = new mem_at("sys_mem");
    ppi_at_ = new ppi_at("sys_ppi");
    fio_at_ = new fio_at("sys_fio");

    cpu_->cpu2biu_initiator_socket.bind(router_at_->cpu2biu_target_socket);
    cpu_->cpu2nice_initiator_socket.bind(nice_at_->cpu2nice_target_socket);
    router_at_->router2clint_initiator_socket.bind(clint_at_->router2clint_target_socket);
    router_at_->router2plic_initiator_socket.bind(plic_at_->router2plic_target_socket);
    router_at_->router2mem_initiator_socket.bind(mem_at_->router2mem_target_socket);
    router_at_->router2ppi_initiator_socket.bind(ppi_at_->router2ppi_target_socket);
    router_at_->router2fio_initiator_socket.bind(fio_at_->router2fio_target_socket);

    cout << module_name << " created !" << endl;
}

e203_soc::~e203_soc()
{
    delete fio_at_;
    delete ppi_at_;
    delete mem_at_;
    delete plic_at_;
    delete clint_at_;
    delete nice_at_;
    delete router_at_;
    delete cpu_;
}
