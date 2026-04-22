#include "models/peripherals/mem_at.h"
#include <iostream>

using namespace std;
using namespace sc_core;

mem_at::mem_at(sc_module_name module_name)
    : sc_module(module_name)
{
    cout << module_name << " created !" << endl;
}

mem_at::~mem_at() {}

