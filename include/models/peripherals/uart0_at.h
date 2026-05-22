#pragma once

#include <array>
#include <cstdint>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

class uart0_at : public sc_core::sc_module
{
private:
    static constexpr uint32_t kBaseAddr = 0x10013000u;
    static constexpr uint32_t kRegisterCount = 10u;
    static constexpr uint32_t kRegisterBytes = 4u;
    static constexpr uint32_t kSizeBytes = kRegisterCount * kRegisterBytes;
    static constexpr uint32_t kEndAddr = kBaseAddr + kSizeBytes;
    static constexpr uint32_t kThrOffset = 0x00u;
    static constexpr uint32_t kLsrOffset = 0x14u;
    static constexpr uint32_t kLsrDefault = 0x60u;

    std::array<uint32_t, kRegisterCount> regs_{};

    static uint32_t offset_of(sc_dt::uint64 addr);
    bool access_valid(tlm::tlm_generic_payload& trans) const;
    uint32_t load_word(uint32_t offset) const;
    void store_word(uint32_t offset, uint32_t value);

public:
    tlm_utils::simple_target_socket<uart0_at> router2uart0_target_socket;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    uart0_at(sc_core::sc_module_name module_name);
    ~uart0_at();
};
