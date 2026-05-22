#include "models/core/core_ca.h"
#include "common/debug_logger.h"
#include "models/bus/biu_ca.h"
#include "models/core/exu_ca.h"
#include "models/core/ifu_ca.h"

using namespace sc_core;

core_ca::core_ca(sc_module_name module_name, const e203sim::sim_config &config)
    : sc_module(module_name),
      ifu_exu_pipe_("ifu_exu_pipe")
{
    biu_ = new biu_ca("biu_ca");
    ifu_ = new ifu_ca("ifu_ca", config);
    exu_ = new exu_ca("exu_ca", config);
    // 这是 CPU 内部唯一显式建模的短流水线通道：
    // IFU 作为 producer 驱动 valid/payload，把 ifu_exu_packet 送给 EXU；
    // EXU 作为 consumer 驱动 ready，决定是否接收当前 packet；
    // EXU 也复用同一个 channel 的后向 flush 通道，把重定向 PC 送回 IFU。
    ifu_->bind_pipeline(ifu_exu_pipe_);
    exu_->bind_pipeline(ifu_exu_pipe_);
    // 指令侧 ITCM socket 直接由 IFU 发起。
    ifu_->ifu2itcm_initiator_socket.bind(coreifu2itcm_initiator_socket);
    // 数据侧 socket 由 EXU 内部 LSU 发起，core_ca 只做层级转接。
    exu_->lsu().lsu2dtcm_initiator_socket.bind(core2dtcm_initiator_socket);
    exu_->lsu().lsu2itcm_initiator_socket.bind(corelsu2itcm_initiator_socket);
    // LSU 的 non-blocking BIU 请求先进入 biu_ca，再由 biu_ca 转成 blocking AT 访问 router。
    exu_->lsu().lsu2biu_initiator_socket.bind(biu_->lsu2biu_target_socket);
    biu_->biu2router_initiator_socket.bind(corebiu2router_initiator_socket);
    INFO(module_name << " created !");
}

core_ca::~core_ca() {
    delete exu_;
    delete ifu_;
    delete biu_;
}
