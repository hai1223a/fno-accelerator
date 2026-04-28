#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

class core_ca;
class memory_ca;
class cpu : public sc_core::sc_module
{
private:
    core_ca *core_ca_;
    memory_ca *itlm_ca_;
    memory_ca *dtlm_ca_;
public:
    tlm::tlm_initiator_socket<> cpu2biu_initiator_socket;
    tlm::tlm_initiator_socket<> cpu2nice_initiator_socket;

    cpu(sc_core::sc_module_name module_name);
    ~cpu();
};
