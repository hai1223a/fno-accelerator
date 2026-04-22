#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

class core_at;
class memory_at;
class cpu_at : public sc_core::sc_module
{
private:
    core_at *core_at_;
    memory_at *itlm_at_;
    memory_at *dtlm_at_;
public:
    tlm_utils::simple_initiator_socket<cpu_at> cpu2biu_initiator_socket;

    cpu_at(sc_core::sc_module_name module_name);
    ~cpu_at();
};
