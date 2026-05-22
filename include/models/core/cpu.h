#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "common/types.h"

class core_ca;
class dtcm_ca;
class itcm_ca;
class cpu : public sc_core::sc_module
{
private:
    core_ca *core_ca_;
    itcm_ca *itcm_ca_;
    dtcm_ca *dtcm_ca_;
public:
    // CPU 内部 core BIU 发往 SoC router/system bus 的出口。
    tlm::tlm_initiator_socket<> cpubiu2router_initiator_socket;
    tlm::tlm_initiator_socket<> cpu2nice_initiator_socket;

    cpu(sc_core::sc_module_name module_name, const e203sim::sim_config &config);
    ~cpu();

    void load_itcm_binary(const std::string& path, e203sim::addr_t load_addr);
};
