#pragma once
#include <systemc>
#include "common/types.h"
class cpu;
class router_at;
class clint_at;
class plic_at;
class mem_at;
class ppi_at;
class fio_at;
class nice_ca;

class e203_soc: public sc_core::sc_module
{
private:
    cpu *cpu_;
    nice_ca *nice_ca_;
    router_at *router_at_;
    clint_at *clint_at_;
    plic_at *plic_at_;
    mem_at *mem_at_;
    ppi_at *ppi_at_;
    fio_at *fio_at_;
public:
    e203_soc(sc_core::sc_module_name module_name, const e203sim::sim_config &config);
    ~e203_soc();

};
