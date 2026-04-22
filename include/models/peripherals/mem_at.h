#pragma once

#include <systemc>

class mem_at : public sc_core::sc_module
{
private:

public:
    mem_at(sc_core::sc_module_name module_name);
    ~mem_at();
};

