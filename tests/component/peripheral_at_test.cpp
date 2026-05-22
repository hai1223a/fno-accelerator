#include "common/debug_logger.h"
#include "models/peripherals/clint_at.h"
#include "models/peripherals/uart0_at.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <tlm_utils/simple_initiator_socket.h>

namespace {

[[noreturn]] void fail(const std::string& msg)
{
    std::cerr << "[TEST][FAIL] " << sc_core::sc_time_stamp() << " " << msg << std::endl;
    std::exit(1);
}

void expect(bool cond, const std::string& msg)
{
    if (!cond) {
        fail(msg);
    }
}

uint32_t read_le32(const std::array<unsigned char, 4>& data)
{
    return (static_cast<uint32_t>(data[0]) << 0) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void store_le32(std::array<unsigned char, 4>& data, uint32_t value)
{
    data[0] = static_cast<unsigned char>((value >> 0) & 0xffu);
    data[1] = static_cast<unsigned char>((value >> 8) & 0xffu);
    data[2] = static_cast<unsigned char>((value >> 16) & 0xffu);
    data[3] = static_cast<unsigned char>((value >> 24) & 0xffu);
}

void init_trans(tlm::tlm_generic_payload& trans,
                tlm::tlm_command command,
                uint32_t addr,
                std::array<unsigned char, 4>& data)
{
    trans.set_command(command);
    trans.set_address(addr);
    trans.set_data_ptr(data.data());
    trans.set_data_length(4);
    trans.set_byte_enable_ptr(nullptr);
    trans.set_byte_enable_length(0);
    trans.set_streaming_width(4);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
}

class dummy_initiator : public sc_core::sc_module
{
public:
    tlm_utils::simple_initiator_socket<dummy_initiator> socket;

    explicit dummy_initiator(sc_core::sc_module_name name)
        : sc_core::sc_module(name), socket("socket")
    {
    }
};

uint32_t read32(clint_at& clint, uint32_t addr)
{
    std::array<unsigned char, 4> data{};
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    init_trans(trans, tlm::TLM_READ_COMMAND, addr, data);
    clint.b_transport(trans, delay);
    expect(trans.get_response_status() == tlm::TLM_OK_RESPONSE, "CLINT read should be OK");
    expect(delay == sc_core::SC_ZERO_TIME, "CLINT read should not annotate delay");
    return read_le32(data);
}

void write32(uart0_at& uart0, uint32_t addr, uint32_t value)
{
    std::array<unsigned char, 4> data{};
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    store_le32(data, value);
    init_trans(trans, tlm::TLM_WRITE_COMMAND, addr, data);
    uart0.b_transport(trans, delay);
    expect(trans.get_response_status() == tlm::TLM_OK_RESPONSE, "UART0 write should be OK");
    expect(delay == sc_core::SC_ZERO_TIME, "UART0 write should not annotate delay");
}

void test_uart0_thr_print(uart0_at& uart0)
{
    std::ostringstream captured;
    auto* old_buf = std::cout.rdbuf(captured.rdbuf());

    write32(uart0, 0x10013000u, 'A');
    write32(uart0, 0x10013004u, 'B');

    std::cout.rdbuf(old_buf);
    expect(captured.str() == "A", "UART0 should print only THR low byte writes");
}

void test_clint_mtime(clint_at& clint)
{
    expect(read32(clint, 0x02000000u) == 0u,
           "CLINT mtime low should start at 0");

    sc_core::sc_start(sc_core::sc_time(30, sc_core::SC_NS));
    expect(read32(clint, 0x02000000u) == 3u,
           "CLINT mtime low should increase once per configured cycle");
    expect(read32(clint, 0x02000004u) == 0u,
           "CLINT mtime high should be readable as upper 32 bits");
}

} // namespace

int sc_main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    e203sim::debug_logger::instance().disable();

    uart0_at uart0("uart0");
    clint_at clint("clint", 10);
    dummy_initiator uart0_binder("uart0_binder");
    dummy_initiator clint_binder("clint_binder");

    uart0_binder.socket.bind(uart0.router2uart0_target_socket);
    clint_binder.socket.bind(clint.router2clint_target_socket);

    test_uart0_thr_print(uart0);
    test_clint_mtime(clint);

    std::cout << "[TEST][PASS] peripheral_at component test passed" << std::endl;
    return 0;
}
