#pragma once
#include <cstdint>
#include <string>
#include <iostream>
#include <systemc>
namespace e203sim {

using addr_t = uint32_t;
using data_t = uint32_t;
using byte_t = uint8_t;

enum class AccessSize {
    // LSU/EXU 访存宽度，枚举值等于访问字节数。
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
    addr_t reset_pc;
    addr_t load_addr;
    std::string bin_path;
    std::string memory_config_path;
    bool enable_debug;
    std::string debug_path;
    bool enable_pipe_trace;
    std::string pipe_trace_path;
    bool enable_thread_trace;
    std::string thread_trace_path;
    bool enable_difftest;
    std::string difftest_ref_so;
    bool difftest_check_store;
    memory_config *itcm;
    memory_config *dtcm;
    memory_config *clint;
    memory_config *plic;
    memory_config *ppi;
    memory_config *fio;
    void print() const
    {
        std::cout << "cycle_ns       : " << cycle_ns << '\n'
                << "reset_pc       : 0x" << std::hex << reset_pc << std::dec << '\n'
                << "load_addr      : 0x" << std::hex << load_addr << std::dec << '\n'
                << "bin_path       : " << bin_path << '\n'
                << "memory_config  : " << memory_config_path << '\n'
                << "enable_debug   : " << enable_debug << '\n'
                << "trace_path     : " << debug_path << '\n'
                << "enable_pipe_trace: " << enable_pipe_trace << '\n'
                << "pipe_trace_path: " << pipe_trace_path << '\n'
                << "enable_thread_trace: " << enable_thread_trace << '\n'
                << "thread_trace_path: " << thread_trace_path << '\n'
                << "enable_difftest: " << enable_difftest << '\n'
                << "difftest_ref_so: " << difftest_ref_so << '\n'
                << "difftest_check_store: " << difftest_check_store << '\n'
                << "itcm           : " << itcm << '\n'
                << "dtcm           : " << dtcm << '\n'
                << "clint          : " << clint << '\n'
                << "plic           : " << plic << '\n'
                << "ppi            : " << ppi << '\n'
                << "fio            : " << fio << '\n';
    }
};

struct ifu_exu_packet
{
    // IFU 取到的指令 PC；异常 packet 中也作为 faulting PC 使用。
    addr_t pc = 0;
    // 16-bit 指令低 16 位有效；32-bit 指令完整有效。
    uint32_t instr = 0;
    // true 表示 instr 保存 RV32 32-bit 指令，false 表示压缩 16-bit 候选。
    bool instr_32bit = false;
    // pc_valid=false 表示 IFU 已检测到取指异常，EXU 应走 trap commit。
    bool pc_valid = false;
    bool bus_error = false;
    bool misalign = false;
    // IFU mini-decode 暴露的源寄存器编号，用于后续扩展早期 hazard/预测逻辑。
    uint32_t rs1idx = 0;
    uint32_t rs2idx = 0;
    // IFU 静态预测结果；EXU resolve 后若不一致则发 flush。
    bool prdt_taken = false;
    // 预留给 E203 mul/div back-to-back 优化建模，v1 EXU 暂不消费。
    bool muldiv_b2b = false;
    // Konata/O3PipeView trace 元数据：IFU 分配 seq，并记录取指发生的 tick。
    uint64_t trace_seq = 0;
    uint64_t trace_fetch_tick = 0;
};

inline bool operator==(const ifu_exu_packet& lhs, const ifu_exu_packet& rhs)
{
    // SystemC sc_signal<ifu_exu_packet> 需要可比较类型来判断 value_changed。
    return lhs.pc == rhs.pc &&
           lhs.instr == rhs.instr &&
           lhs.instr_32bit == rhs.instr_32bit &&
           lhs.pc_valid == rhs.pc_valid &&
           lhs.bus_error == rhs.bus_error &&
           lhs.misalign == rhs.misalign &&
           lhs.rs1idx == rhs.rs1idx &&
           lhs.rs2idx == rhs.rs2idx &&
           lhs.prdt_taken == rhs.prdt_taken &&
           lhs.muldiv_b2b == rhs.muldiv_b2b &&
           lhs.trace_seq == rhs.trace_seq &&
           lhs.trace_fetch_tick == rhs.trace_fetch_tick;
}

inline bool operator!=(const ifu_exu_packet& lhs, const ifu_exu_packet& rhs)
{
    return !(lhs == rhs);
}

inline std::ostream& operator<<(std::ostream& os, const ifu_exu_packet& pkt)
{
    os << "ifu_exu_packet{pc=0x" << std::hex << pkt.pc
       << ", instr=0x" << pkt.instr
       << std::dec << ", instr_32bit=" << pkt.instr_32bit
       << ", valid=" << pkt.pc_valid
       << ", bus_error=" << pkt.bus_error
       << ", misalign=" << pkt.misalign
       << ", rs1idx=" << pkt.rs1idx
       << ", rs2idx=" << pkt.rs2idx
       << ", prdt_taken=" << pkt.prdt_taken
       << ", muldiv_b2b=" << pkt.muldiv_b2b
       << ", trace_seq=" << pkt.trace_seq
       << ", trace_fetch_tick=" << pkt.trace_fetch_tick << "}";
    return os;
}

inline void sc_trace(sc_core::sc_trace_file* tf, const ifu_exu_packet& pkt, const std::string& name)
{
    // 允许后续打开 waveform 时直接观察 IFU->EXU packet 各字段。
    sc_core::sc_trace(tf, pkt.pc, name + ".pc");
    sc_core::sc_trace(tf, pkt.instr, name + ".instr");
    sc_core::sc_trace(tf, pkt.instr_32bit, name + ".instr_32bit");
    sc_core::sc_trace(tf, pkt.pc_valid, name + ".pc_valid");
    sc_core::sc_trace(tf, pkt.bus_error, name + ".bus_error");
    sc_core::sc_trace(tf, pkt.misalign, name + ".misalign");
    sc_core::sc_trace(tf, pkt.rs1idx, name + ".rs1idx");
    sc_core::sc_trace(tf, pkt.rs2idx, name + ".rs2idx");
    sc_core::sc_trace(tf, pkt.prdt_taken, name + ".prdt_taken");
    sc_core::sc_trace(tf, pkt.muldiv_b2b, name + ".muldiv_b2b");
    sc_core::sc_trace(tf, pkt.trace_seq, name + ".trace_seq");
    sc_core::sc_trace(tf, pkt.trace_fetch_tick, name + ".trace_fetch_tick");
}

} // namespace e203sim
