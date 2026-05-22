#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>

class clint_at : public sc_core::sc_module
{
private:
    static constexpr uint32_t kBaseAddr = 0x02000000u;
    static constexpr uint32_t kRegisterBytes = 4u;
    static constexpr uint32_t kMtimeLowOffset = 0x00u;
    static constexpr uint32_t kMtimeHighOffset = 0x04u;
    static constexpr uint32_t kEndAddr = kBaseAddr + 8u;

    uint64_t mtime_base_ = 0;
    sc_core::sc_time mtime_base_time_ = sc_core::SC_ZERO_TIME;
    sc_core::sc_time clk_period_;

    static uint32_t offset_of(sc_dt::uint64 addr);
    bool access_valid(tlm::tlm_generic_payload& trans) const;
    uint64_t current_mtime() const;

public:
    tlm_utils::simple_target_socket<clint_at> router2clint_target_socket;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    clint_at(sc_core::sc_module_name module_name, uint32_t cycle_ns = 10);
    ~clint_at();
};
