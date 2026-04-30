#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include "models/memory/sram_ca.h"
#include "common/types.h"

class itcm_ca : public sc_core::sc_module, public sram_ca
{
private:
    enum class trans_source {
        lsu,
        ifu
    };

    struct pending_request {
        tlm::tlm_generic_payload* trans = nullptr;
    };

    e203sim::memory_config config;
    sc_core::sc_time clk_period;
    pending_request lsu_pending_;
    pending_request ifu_pending_;
    bool lsu_active_ = false;
    bool ifu_active_ = false;
    sc_core::sc_event dispatch_event_;
    tlm::tlm_sync_enum accept_request(tlm::tlm_generic_payload& trans,
                                      tlm::tlm_phase& phase,
                                      sc_core::sc_time& delay,
                                      trans_source source);
    bool source_busy(trans_source source) const;
    bool has_pending_request() const;
    pending_request& pending_slot(trans_source source);
    trans_source select_next_source() const;
    void set_active(trans_source source, bool active);
    void access_memory(tlm::tlm_generic_payload& trans, trans_source source);
    void resp_thread();

public:
    SC_HAS_PROCESS(itcm_ca);
    tlm_utils::simple_target_socket<itcm_ca, 64> lsu2itcm_target_socket;
    tlm_utils::simple_target_socket<itcm_ca, 64> ifu2itcm_target_socket;
    tlm::tlm_sync_enum nb_transport_fw_lsu(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay);
    tlm::tlm_sync_enum nb_transport_fw_ifu(tlm::tlm_generic_payload& trans,
                                           tlm::tlm_phase& phase,
                                           sc_core::sc_time& delay);
    itcm_ca(sc_core::sc_module_name module_name, e203sim::memory_config* cfg, uint32_t cycle_unit);
    ~itcm_ca();
};
