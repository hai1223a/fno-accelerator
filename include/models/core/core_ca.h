#pragma once

#include <cstdint>
#include <memory>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "common/pipeline_if.h"
#include "common/types.h"

class biu_ca;
class exu_ca;
class ifu_ca;

class core_ca: public sc_core::sc_module
{
private:
    // core_ca 只负责结构连接：IFU 取指、EXU 执行/提交，LSU 由 EXU 持有。
    biu_ca* biu_;
    exu_ca* exu_;
    ifu_ca* ifu_;
    // IFU->EXU 前向 packet 与 EXU->IFU flush 共享同一个 pipeline_channel。
    // 前向数据类型是 ifu_exu_packet，后向 flush 数据类型是 addr_t 目标 PC。
    e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t> ifu_exu_pipe_;

public:
    SC_HAS_PROCESS(core_ca);
    tlm_utils::simple_initiator_socket<core_ca> core2nice_initiator_socket;
    core_ca(sc_core::sc_module_name module_name, const e203sim::sim_config &config);
    ~core_ca();

    // core 对外保持原有 socket 形态；内部由 IFU/EXU.LSU/BIU 分别绑定。
    tlm::tlm_initiator_socket<> core2dtcm_initiator_socket;
    tlm::tlm_initiator_socket<64> corelsu2itcm_initiator_socket;
    tlm::tlm_initiator_socket<64> coreifu2itcm_initiator_socket;
    // core 内部 BIU 发往 SoC router/system bus 的出口。
    tlm::tlm_initiator_socket<> corebiu2router_initiator_socket;
    // tlm::tlm_initiator_socket<> core_lsu2itcm_initiator_socket;
};
