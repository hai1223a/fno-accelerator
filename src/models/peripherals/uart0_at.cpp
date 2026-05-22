#include "models/peripherals/uart0_at.h"

#include "common/debug_logger.h"

#include <iostream>

using namespace sc_core;

namespace {

uint32_t read_le32(const unsigned char* data)
{
    return (static_cast<uint32_t>(data[0]) << 0) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void write_le32(unsigned char* data, uint32_t value)
{
    data[0] = static_cast<unsigned char>((value >> 0) & 0xffu);
    data[1] = static_cast<unsigned char>((value >> 8) & 0xffu);
    data[2] = static_cast<unsigned char>((value >> 16) & 0xffu);
    data[3] = static_cast<unsigned char>((value >> 24) & 0xffu);
}

} // namespace

uart0_at::uart0_at(sc_module_name module_name)
    : sc_module(module_name)
{
    regs_[kLsrOffset / kRegisterBytes] = kLsrDefault;
    router2uart0_target_socket.register_b_transport(this, &uart0_at::b_transport);
    INFO(module_name << " created !");
}

uart0_at::~uart0_at() = default;

uint32_t uart0_at::offset_of(sc_dt::uint64 addr)
{
    return static_cast<uint32_t>(addr - kBaseAddr);
}

bool uart0_at::access_valid(tlm::tlm_generic_payload& trans) const
{
    const sc_dt::uint64 addr = trans.get_address();
    if (addr < kBaseAddr || addr >= kEndAddr) {
        return false;
    }

    const uint32_t len = trans.get_data_length();
    const uint32_t offset = offset_of(addr);
    return (offset % kRegisterBytes) == 0 &&
           len == kRegisterBytes &&
           trans.get_data_ptr() != nullptr;
}

uint32_t uart0_at::load_word(uint32_t offset) const
{
    return regs_[offset / kRegisterBytes];
}

void uart0_at::store_word(uint32_t offset, uint32_t value)
{
    regs_[offset / kRegisterBytes] = value;
    if (offset == kThrOffset) {
        std::cout << static_cast<char>(value & 0xffu);
        std::cout.flush();
    }
}

void uart0_at::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    delay = SC_ZERO_TIME;
    INFO("[" << sc_time_stamp() << "] "
             << name()
             << " b_transport addr=0x" << std::hex << trans.get_address() << std::dec);

    if (!access_valid(trans)) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    const uint32_t offset = offset_of(trans.get_address());
    if (trans.get_command() == tlm::TLM_READ_COMMAND) {
        write_le32(trans.get_data_ptr(), load_word(offset));
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
        store_word(offset, read_le32(trans.get_data_ptr()));
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    } else {
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    }
}
