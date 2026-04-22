#pragma once
#include <systemc>
class cpu_at;
class router_at;
class clint_at;
class mem_at;
class ppi_at;

class e203_soc: public sc_core::sc_module
{
private:
    cpu_at *cpu_at_;
    router_at *router_at_;
    clint_at *clint_at_;
    mem_at *mem_at_;
    ppi_at *ppi_at_;
public:
    e203_soc(sc_core::sc_module_name module_name);
    ~e203_soc();

};
