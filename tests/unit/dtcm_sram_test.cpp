#include "models/memory/sram_ca.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <systemc>

namespace {

[[noreturn]] void fail(const std::string& msg)
{
    std::cerr << "[TEST][FAIL] " << msg << std::endl;
    std::exit(1);
}

void expect(bool cond, const std::string& msg)
{
    if (!cond) {
        fail(msg);
    }
}

uint64_t read_lane(sram_ca& sram, uint32_t addr)
{
    uint64_t value = 0;
    expect(sram.sram_read(addr, value), "sram_read unexpectedly failed");
    return value;
}

void test_word_write_read()
{
    sram_ca sram(32, 64);
    expect(sram.sram_write(0, 0x11223344u, 0x0f), "word write should succeed");
    expect(read_lane(sram, 0) == 0x11223344u, "word readback mismatch");
}

void test_lane_alignment()
{
    sram_ca sram(32, 64);
    expect(sram.sram_write(0, 0x11223344u, 0x0f), "initial word write should succeed");
    expect(read_lane(sram, 1) == 0x11223344u, "address 1 should read same 32-bit lane");
    expect(read_lane(sram, 2) == 0x11223344u, "address 2 should read same 32-bit lane");
    expect(read_lane(sram, 3) == 0x11223344u, "address 3 should read same 32-bit lane");
}

void test_byte_strobes()
{
    sram_ca sram(32, 64);
    expect(sram.sram_write(0, 0x11223344u, 0x0f), "initial word write should succeed");
    expect(sram.sram_write(2, 0xaabbaabbu, 0x0c), "high halfword write should succeed");
    expect(read_lane(sram, 0) == 0xaabb3344u, "high halfword strobe mismatch");

    expect(sram.sram_write(1, 0xccccccccu, 0x02), "middle byte write should succeed");
    expect(read_lane(sram, 0) == 0xaabbcc44u, "middle byte strobe mismatch");

    expect(sram.sram_write(0, 0xffffffffu, 0x00), "zero strobe write should succeed");
    expect(read_lane(sram, 0) == 0xaabbcc44u, "zero strobe should not change lane");
}

void test_little_endian_lane()
{
    sram_ca sram(32, 64);
    expect(sram.sram_write(0, 0x00000078u, 0x01), "byte 0 write should succeed");
    expect(sram.sram_write(0, 0x00005600u, 0x02), "byte 1 write should succeed");
    expect(sram.sram_write(0, 0x00340000u, 0x04), "byte 2 write should succeed");
    expect(sram.sram_write(0, 0x12000000u, 0x08), "byte 3 write should succeed");
    expect(read_lane(sram, 0) == 0x12345678u, "little-endian byte lane packing mismatch");
}

void test_bounds()
{
    sram_ca sram(32, 8);
    uint64_t value = 0;
    expect(sram.sram_write(4, 0xdeadbeefu, 0x0f), "last aligned word write should succeed");
    expect(sram.sram_read(4, value), "last aligned word read should succeed");
    expect(!sram.sram_write(8, 0x12345678u, 0x0f), "write at one-past-end should fail");
    expect(!sram.sram_read(8, value), "read at one-past-end should fail");
}

} // namespace

int sc_main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    test_word_write_read();
    test_lane_alignment();
    test_byte_strobes();
    test_little_endian_lane();
    test_bounds();

    std::cout << "[TEST][PASS] dtcm_sram unit test passed" << std::endl;
    return 0;
}
