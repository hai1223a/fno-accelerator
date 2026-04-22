#pragma once

#include <systemc>

class clint_at : public sc_core::sc_module
{
private:

public:
    clint_at(sc_core::sc_module_name module_name);
    ~clint_at();
};

