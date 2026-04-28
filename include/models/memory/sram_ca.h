#pragma once
#include <stdint.h>
#include <vector>
class sram_ca
{
private:
    std::vector<uint8_t> mem;
    uint32_t size;
    uint32_t portwidth;
    uint32_t port_bytes;
public:
    sram_ca(uint32_t portwidth, uint32_t size);
    ~sram_ca();
    bool sram_write(uint32_t addr, uint64_t value, uint8_t strobe);
    bool sram_read(uint32_t addr, uint64_t& value);
};

