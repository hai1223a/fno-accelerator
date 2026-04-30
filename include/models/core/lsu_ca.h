#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <array>
#include "common/types.h"
class lsu_ca : public sc_core::sc_module
{
private:
    struct trans_ctx {
        tlm::tlm_generic_payload trans;
        std::array<uint8_t, 4> data{};
        std::array<uint8_t, 4> byte_enable{};
        uint32_t addr = 0;
        uint8_t size = 0;
    };

    enum class test_state {
        store_word,
        read_after_store_word,
        store_half_high,
        read_after_store_half,
        store_byte_mid,
        read_after_store_byte,
        done
    };

    sc_core::sc_time clk_period_;
    std::array<e203sim::memory_config*, 2> memories_{};
    test_state test_state_ = test_state::store_word;
    int test_count_ = 1;
    uint32_t test_addr_ = 0x90000000;
    bool done_logged_ = false;
    std::unique_ptr<trans_ctx> outstanding_;
    sc_core::sc_event resp_event_;

    void thread();
    void issue_next_test();
    void send_test_dtcm(std::unique_ptr<trans_ctx> ctx);
    void send_test_itcm(std::unique_ptr<trans_ctx> ctx);
    void send_test_biu(std::unique_ptr<trans_ctx> ctx);
    void handle_response(tlm::tlm_generic_payload& trans);

    std::unique_ptr<trans_ctx> make_store_word(uint32_t addr, uint32_t value);
    std::unique_ptr<trans_ctx> make_store_half(uint32_t addr, uint16_t value);
    std::unique_ptr<trans_ctx> make_store_byte(uint32_t addr, uint8_t value);
    std::unique_ptr<trans_ctx> make_load_word(uint32_t addr);

public:
    SC_HAS_PROCESS(lsu_ca);

    tlm_utils::simple_initiator_socket<lsu_ca> lsu2dtcm_initiator_socket;
    tlm_utils::simple_initiator_socket<lsu_ca, 64> lsu2itcm_initiator_socket;
    tlm_utils::simple_initiator_socket<lsu_ca> lsu2biu_initiator_socket;

    lsu_ca(sc_core::sc_module_name module_name, const e203sim::sim_config &config);
    ~lsu_ca();

    tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay);
};
