#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "common/types.h"

class ifu_ca : public sc_core::sc_module
{
private:
    struct trans_ctx {
        tlm::tlm_generic_payload trans;
        std::array<uint8_t, 8> data{};
        uint32_t addr = 0;
    };

    std::unique_ptr<trans_ctx> outstanding_;

    void send_read32(std::unique_ptr<trans_ctx> ctx);
    void handle_response(tlm::tlm_generic_payload& trans);
    std::unique_ptr<trans_ctx> make_read32(uint32_t addr);

public:
    SC_HAS_PROCESS(ifu_ca);

    tlm_utils::simple_initiator_socket<ifu_ca, 64> ifu2itcm_initiator_socket;

    ifu_ca(sc_core::sc_module_name module_name, const e203sim::sim_config& config);
    ~ifu_ca();

    void issue_read32(uint32_t addr);

    tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay);
};
