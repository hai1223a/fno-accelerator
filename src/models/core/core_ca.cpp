#include "models/core/core_ca.h"
#include "common/debug_logger.h"
#include "models/core/ifu_ca.h"
#include "models/core/lsu_ca.h"

using namespace sc_core;

core_ca::core_ca(sc_module_name module_name, const e203sim::sim_config &config)
    : sc_module(module_name)
{
    ifu_ = new ifu_ca("ifu_ca", config);
    lsu_ = new lsu_ca("lsu_ca", config);
    ifu_->ifu2itcm_initiator_socket.bind(coreifu2itcm_initiator_socket);
    lsu_->lsu2dtcm_initiator_socket.bind(core2dtcm_initiator_socket);
    lsu_->lsu2itcm_initiator_socket.bind(corelsu2itcm_initiator_socket);
    lsu_->lsu2biu_initiator_socket.bind(core2biu_initiator_socket);
    INFO(module_name << " created !");
}

core_ca::~core_ca() {
    delete lsu_;
    delete ifu_;
}


