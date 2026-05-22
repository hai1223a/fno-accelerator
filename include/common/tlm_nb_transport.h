#pragma once

#include "common/debug_logger.h"

#include <systemc>
#include <tlm>

namespace e203sim {

inline const char* tlm_phase_name(const tlm::tlm_phase& phase)
{
    if (phase == tlm::BEGIN_REQ) {
        return "BEGIN_REQ";
    }
    if (phase == tlm::END_REQ) {
        return "END_REQ";
    }
    if (phase == tlm::BEGIN_RESP) {
        return "BEGIN_RESP";
    }
    if (phase == tlm::END_RESP) {
        return "END_RESP";
    }
    return "UNKNOWN_PHASE";
}

inline const char* tlm_sync_name(tlm::tlm_sync_enum value)
{
    switch (value) {
    case tlm::TLM_ACCEPTED:
        return "TLM_ACCEPTED";
    case tlm::TLM_UPDATED:
        return "TLM_UPDATED";
    case tlm::TLM_COMPLETED:
        return "TLM_COMPLETED";
    }
    return "UNKNOWN_SYNC";
}

// non-blocking TLM initiator 的公共基类。
// 派生模块仍然负责保存 payload/context 的生命周期；这里统一管理 outstanding 检查、
// BEGIN_REQ 发起日志，以及 BEGIN_RESP -> END_RESP 的 phase 收束。
class nb_transport_initiator_base
{
protected:
    nb_transport_initiator_base() = default;
    ~nb_transport_initiator_base() = default;

    nb_transport_initiator_base(const nb_transport_initiator_base&) = delete;
    nb_transport_initiator_base& operator=(const nb_transport_initiator_base&) = delete;

    bool has_nb_outstanding() const
    {
        return outstanding_payload_ != nullptr;
    }

    void mark_nb_outstanding(tlm::tlm_generic_payload& trans)
    {
        SIM_ASSERT(outstanding_payload_ == nullptr, "non-blocking initiator already has outstanding payload");
        outstanding_payload_ = &trans;
    }

    void clear_nb_outstanding(tlm::tlm_generic_payload& trans)
    {
        SIM_ASSERT(outstanding_payload_ == &trans, "response payload does not match outstanding payload");
        outstanding_payload_ = nullptr;
    }

    template <typename SocketT>
    tlm::tlm_sync_enum send_nb_begin_req(SocketT& socket,
                                         tlm::tlm_generic_payload& trans,
                                         const char* module_name,
                                         const char* detail)
    {
        mark_nb_outstanding(trans);

        tlm::tlm_phase phase(tlm::BEGIN_REQ);
        sc_core::sc_time delay(sc_core::SC_ZERO_TIME);

        SIM_INFO(module_name, "发起BEGIN_REQ: addr=0x"
                              << std::hex << trans.get_address() << std::dec
                              << ", " << detail);

        const tlm::tlm_sync_enum res = socket->nb_transport_fw(trans, phase, delay);
        SIM_INFO(module_name, "收到fw返回: sync=" << tlm_sync_name(res)
                              << ", phase=" << tlm_phase_name(phase)
                              << ", delay=" << delay);
        return res;
    }

    bool complete_nb_begin_resp(tlm::tlm_generic_payload& trans,
                                tlm::tlm_phase& phase,
                                const char* module_name)
    {
        if (phase != tlm::BEGIN_RESP) {
            return false;
        }

        SIM_INFO(module_name, "收到BEGIN_RESP, addr=0x"
                              << std::hex << trans.get_address() << std::dec);
        clear_nb_outstanding(trans);
        phase = tlm::END_RESP;
        SIM_INFO(module_name, "返回END_RESP，事务完成");
        return true;
    }

private:
    tlm::tlm_generic_payload* outstanding_payload_ = nullptr;
};

// target 收到 BEGIN_REQ 时的公共处理：busy 检查、保存 pending、返回 END_REQ。
inline tlm::tlm_sync_enum accept_nb_begin_req(tlm::tlm_generic_payload*& pending,
                                             tlm::tlm_generic_payload& trans,
                                             tlm::tlm_phase& phase,
                                             sc_core::sc_time& delay,
                                             sc_core::sc_event& event,
                                             const sc_core::sc_time& event_delay,
                                             const char* module_name)
{
    if (phase == tlm::BEGIN_REQ) {
        if (pending != nullptr) {
            SIM_INFO(module_name, "target busy, BEGIN_REQ accepted later");
            return tlm::TLM_ACCEPTED;
        }

        pending = &trans;
        event.notify(event_delay);
        phase = tlm::END_REQ;
        delay = sc_core::SC_ZERO_TIME;
        return tlm::TLM_UPDATED;
    }

    if (phase == tlm::END_RESP) {
        return tlm::TLM_COMPLETED;
    }

    return tlm::TLM_ACCEPTED;
}

} // namespace e203sim
