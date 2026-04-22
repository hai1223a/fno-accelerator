#include "models/peripherals/clint_at.h"
#include <iostream>

using namespace std;
using namespace sc_core;

clint_at::clint_at(sc_module_name module_name)
    : sc_module(module_name)
{
    cout << module_name << " created !" << endl;
}

clint_at::~clint_at() {}

