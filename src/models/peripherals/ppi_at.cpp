#include "models/peripherals/ppi_at.h"
#include <iostream>

using namespace std;
using namespace sc_core;

ppi_at::ppi_at(sc_module_name module_name)
    : sc_module(module_name)
{
    cout << module_name << " created !" << endl;
}

ppi_at::~ppi_at() {}

