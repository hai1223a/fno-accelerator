#include "models/memory/sram_ca.h"
#include "common/debug_logger.h"
#include <iostream>
#include <systemc>

sram_ca::sram_ca(uint32_t portwidth, uint32_t size)
    :mem(size, 0), size(size), portwidth(portwidth)
    {
        SIM_ASSERT(portwidth == 32 || portwidth == 64, "sram_ca only supports 32/64-bit port width");
        port_bytes = portwidth / 8;
    }

sram_ca::~sram_ca(){}


bool sram_ca::sram_read(uint32_t addr, uint64_t &value)
{
    addr = addr & ~(port_bytes - 1);
    if (addr + port_bytes > size) return false;
    value = 0;
    for (size_t i = 0; i < port_bytes; i++)
    {
        value |= static_cast<uint64_t>(mem[addr + i]) << (8 * i);
    }
    return true;
}

bool sram_ca::sram_write(uint32_t addr, uint64_t value, uint8_t strobe)
{
    addr = addr & ~(port_bytes - 1);
    if (addr + port_bytes > size) return false;

    for (size_t i = 0; i < port_bytes; i++)
    {
        if (strobe & (1u << i)) {
            mem[addr + i] =
                static_cast<uint8_t>((value >> (i * 8)) & 0xff);
        }
    }
    return true;
}