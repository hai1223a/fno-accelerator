#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

class core_at;
class memory_at;
class cpu : public sc_core::sc_module
{
private:
    core_at *core_at_;
    memory_at *itlm_at_;
    memory_at *dtlm_at_;
public:
    tlm::tlm_initiator_socket<> cpu2biu_initiator_socket;
    tlm::tlm_initiator_socket<> cpu2nice_initiator_socket;

    cpu(sc_core::sc_module_name module_name);
    ~cpu();
};
