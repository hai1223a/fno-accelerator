#pragma once

#include <systemc>

class ppi_at : public sc_core::sc_module
{
private:

public:
    ppi_at(sc_core::sc_module_name module_name);
    ~ppi_at();
};

