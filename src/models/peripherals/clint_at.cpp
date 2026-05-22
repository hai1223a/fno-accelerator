#include "models/peripherals/clint_at.h"
#include "common/debug_logger.h"
#include <iostream>

using namespace std;
using namespace sc_core;

clint_at::clint_at(sc_module_name module_name, uint32_t cycle_ns)
    : sc_module(module_name), clk_period_(cycle_ns, SC_NS)
{
    router2clint_target_socket.register_b_transport(this, &clint_at::b_transport);
    INFO(module_name << " created !");
}

clint_at::~clint_at() {}

uint32_t clint_at::offset_of(sc_dt::uint64 addr)
{
    return static_cast<uint32_t>(addr - kBaseAddr);
}

bool clint_at::access_valid(tlm::tlm_generic_payload& trans) const
{
    const sc_dt::uint64 addr = trans.get_address();
    if (addr < kBaseAddr || addr >= kEndAddr) {
        return false;
    }

    const uint32_t offset = offset_of(addr);
    return (offset == kMtimeLowOffset || offset == kMtimeHighOffset) &&
           trans.get_data_length() == kRegisterBytes &&
           trans.get_data_ptr() != nullptr;
}

uint64_t clint_at::current_mtime() const
{
    const sc_time elapsed = sc_time_stamp() - mtime_base_time_;
    return mtime_base_ + static_cast<uint64_t>(elapsed / clk_period_);
}

void clint_at::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    delay = SC_ZERO_TIME;
    INFO("[" << sc_time_stamp() << "] "
                          << name()
                          << " b_transport addr=0x" << hex << trans.get_address() << dec);

    if (!access_valid(trans)) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    if (trans.get_command() == tlm::TLM_READ_COMMAND) {
        const uint64_t mtime = current_mtime();
        const uint32_t offset = offset_of(trans.get_address());
        const uint32_t word = (offset == kMtimeLowOffset)
                                  ? static_cast<uint32_t>(mtime & 0xffffffffu)
                                  : static_cast<uint32_t>((mtime >> 32) & 0xffffffffu);
        auto* data = trans.get_data_ptr();
        data[0] = static_cast<unsigned char>((word >> 0) & 0xffu);
        data[1] = static_cast<unsigned char>((word >> 8) & 0xffu);
        data[2] = static_cast<unsigned char>((word >> 16) & 0xffu);
        data[3] = static_cast<unsigned char>((word >> 24) & 0xffu);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    } else {
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    }
}
