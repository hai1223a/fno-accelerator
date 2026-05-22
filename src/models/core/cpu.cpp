#include "models/core/cpu.h"
#include "common/debug_logger.h"
#include "models/core/core_ca.h"
#include "models/memory/dtcm_ca.h"
#include "models/memory/itcm_ca.h"
#include <iostream>
#include <fstream>
#include <vector>

using namespace std;
using namespace sc_core;

cpu::cpu(sc_module_name module_name, const e203sim::sim_config &config)
    : sc_module(module_name)
{
    core_ca_ = new core_ca("core", config);
    itcm_ca_ = new itcm_ca("itcm", config.itcm, config.cycle_ns);
    dtcm_ca_ = new dtcm_ca("dtcm", config.dtcm, config.cycle_ns);

    core_ca_->corelsu2itcm_initiator_socket.bind(itcm_ca_->lsu2itcm_target_socket);
    core_ca_->coreifu2itcm_initiator_socket.bind(itcm_ca_->ifu2itcm_target_socket);
    core_ca_->core2dtcm_initiator_socket.bind(dtcm_ca_->lsu2dtcm_target_socket);
    core_ca_->corebiu2router_initiator_socket.bind(cpubiu2router_initiator_socket);
    core_ca_->core2nice_initiator_socket.bind(cpu2nice_initiator_socket);

    INFO(module_name << " created !");
}

cpu::~cpu()
{
    delete dtcm_ca_;
    delete itcm_ca_;
    delete core_ca_;
}

void cpu::load_itcm_binary(const std::string& path, e203sim::addr_t load_addr)
{
    if (path.empty()) {
        return;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SIM_ERROR(name(), "cannot open binary image: " << path);
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        SIM_INFO(name(), "binary image is empty: " << path);
        return;
    }

    itcm_ca_->load_binary(load_addr, bytes);

    SIM_INFO(name(), "loaded binary image into ITCM: path=" << path
                    << ", load_addr=0x" << std::hex << load_addr
                    << ", size=0x" << bytes.size() << std::dec);
}
