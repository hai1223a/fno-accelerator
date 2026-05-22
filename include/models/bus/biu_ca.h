#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

// CA 级 BIU：把 core 内部 non-blocking TLM 请求转换成外部 blocking AT 访问。
// v1 先提供 LSU->BIU 一路；后续 IFU 非 ITCM 取指可复用同样的入口/仲裁结构。
class biu_ca : public sc_core::sc_module
{
private:
    // 单 entry 请求 buffer。payload 由上游 initiator 持有，BIU 只保存指针直到 BEGIN_RESP。
    tlm::tlm_generic_payload* pending_ = nullptr;
    bool active_ = false;
    sc_core::sc_event request_event_;

    void thread();

public:
    SC_HAS_PROCESS(biu_ca);

    tlm_utils::simple_target_socket<biu_ca> lsu2biu_target_socket;
    tlm_utils::simple_initiator_socket<biu_ca> biu2router_initiator_socket;

    explicit biu_ca(sc_core::sc_module_name module_name);
    ~biu_ca();

    tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay);
};
