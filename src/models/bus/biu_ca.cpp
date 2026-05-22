#include "models/bus/biu_ca.h"

#include "common/debug_logger.h"
#include "common/tlm_nb_transport.h"
#include "sim/thread_trace.h"

using namespace sc_core;

biu_ca::biu_ca(sc_module_name module_name)
    : sc_module(module_name),
      lsu2biu_target_socket("lsu2biu_target_socket"),
      biu2router_initiator_socket("biu2router_initiator_socket")
{
    lsu2biu_target_socket.register_nb_transport_fw(this, &biu_ca::nb_transport_fw);
    SC_THREAD(thread);
    INFO(module_name << " created !");
}

biu_ca::~biu_ca() = default;

tlm::tlm_sync_enum biu_ca::nb_transport_fw(tlm::tlm_generic_payload& trans,
                                           tlm::tlm_phase& phase,
                                           sc_time& delay)
{
    if (phase == tlm::BEGIN_REQ) {
        // 单 entry buffer 被占用时不接收新请求，上游保持/重试 BEGIN_REQ。
        if (pending_ != nullptr || active_) {
            return tlm::TLM_ACCEPTED;
        }

        pending_ = &trans;
        request_event_.notify(delay);
        phase = tlm::END_REQ;
        delay = SC_ZERO_TIME;
        return tlm::TLM_UPDATED;
    }

    if (phase == tlm::END_RESP) {
        return tlm::TLM_COMPLETED;
    }

    return tlm::TLM_ACCEPTED;
}

void biu_ca::thread()
{
    while (true) {
        wait(request_event_);
        THREAD_TRACE("BIU转换线程", "线程唤醒", "有待处理请求=" << (pending_ != nullptr)
                     << " 正在处理=" << active_);

        while (pending_ != nullptr) {
            auto* trans = pending_;
            pending_ = nullptr;
            active_ = true;

            sc_time delay = SC_ZERO_TIME;
            THREAD_TRACE("BIU转换线程", "转发阻塞访问", "地址=0x" << std::hex
                         << trans->get_address() << std::dec);
            SIM_INFO(basename(), "BIU nb->b 转换: addr=0x"
                                 << std::hex << trans->get_address() << std::dec);
            biu2router_initiator_socket->b_transport(*trans, delay);
            if (delay > SC_ZERO_TIME) {
                wait(delay);
            }

            // 外部 blocking 访问完成后，转换成 non-blocking BEGIN_RESP 返回 LSU。
            tlm::tlm_phase phase = tlm::BEGIN_RESP;
            sc_time rsp_delay = SC_ZERO_TIME;
            THREAD_TRACE("BIU转换线程", "返回响应", "地址=0x" << std::hex
                         << trans->get_address() << std::dec);
            lsu2biu_target_socket->nb_transport_bw(*trans, phase, rsp_delay);
            active_ = false;
        }
    }
}
