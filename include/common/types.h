#pragma once
#include <cstdint>
#include <string>
#include <iostream>
namespace e203sim {

using addr_t = uint32_t;
using data_t = uint32_t;
using byte_t = uint8_t;

enum class AccessSize {
    Byte = 1,
    HalfWord = 2,
    Word = 4,
};

struct args_option
{
    std::string config_path;
    std::string bin_path;

    void print() const
    {
        std::cout << "config_path: " << config_path << '\n'
                << "bin_path   : " << bin_path << '\n';
    }
};

struct memory_config
{
    std::string name;
    addr_t base_addr;
    addr_t size;
    uint32_t read_latency_cycles;
    uint32_t write_latency_cycles;
    void print() const
    {
        std::cout << "memory name: " << name << '\n'
                << "base_addr      : " << std::hex << base_addr  << '\n'
                << "size           : " << size << std::dec << '\n'
                << "read_latency_cycles: " << read_latency_cycles << '\n'
                << "write_latency_cycles: " << write_latency_cycles << '\n';
    }
};

struct sim_config
{
    uint32_t cycle_ns;
    std::string bin_path;
    std::string memory_config_path;
    bool enable_debug;
    std::string debug_path;
    memory_config *itcm;
    memory_config *dtcm;
    memory_config *clint;
    memory_config *plic;
    memory_config *ppi;
    memory_config *fio;
    void print() const
    {
        std::cout << "cycle_ns       : " << cycle_ns << '\n'
                << "bin_path       : " << bin_path << '\n'
                << "memory_config  : " << memory_config_path << '\n'
                << "enable_debug   : " << enable_debug << '\n'
                << "trace_path     : " << debug_path << '\n'
                << "itcm           : " << itcm << '\n'
                << "dtcm           : " << dtcm << '\n'
                << "clint          : " << clint << '\n'
                << "plic           : " << plic << '\n'
                << "ppi            : " << ppi << '\n'
                << "fio            : " << fio << '\n';
    }
};


} // namespace e203sim