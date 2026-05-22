#include "common/debug_logger.h"
#include "common/pipeline_if.h"
#include "common/types.h"
#include "models/core/ifu_ca.h"
#include "models/memory/itcm_ca.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <tlm_utils/simple_initiator_socket.h>

namespace {

constexpr uint32_t kItcmBase = 0x80000000u;
constexpr uint32_t kItcmSize = 0x10000u;
constexpr uint32_t kCycleNs = 10u;
constexpr uint16_t kCnop = 0x0001u;
constexpr uint32_t kAddiX0 = 0x00000013u;
constexpr uint32_t kAddiX1 = 0x00100093u;

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

uint32_t encode_beq(uint32_t rs1, uint32_t rs2, int32_t imm)
{
    const uint32_t uimm = static_cast<uint32_t>(imm) & 0x1fffu;
    return (((uimm >> 12u) & 0x1u) << 31u) |
           (((uimm >> 5u) & 0x3fu) << 25u) |
           ((rs2 & 0x1fu) << 20u) |
           ((rs1 & 0x1fu) << 15u) |
           (((uimm >> 1u) & 0xfu) << 8u) |
           (((uimm >> 11u) & 0x1u) << 7u) |
           0x63u;
}

class dummy_initiator : public sc_core::sc_module
{
public:
    tlm_utils::simple_initiator_socket<dummy_initiator, 64> socket;

    explicit dummy_initiator(sc_core::sc_module_name name)
        : sc_core::sc_module(name), socket("socket")
    {
        socket.register_nb_transport_bw(this, &dummy_initiator::nb_transport_bw);
    }

    tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay)
    {
        (void)trans;
        (void)phase;
        (void)delay;
        return tlm::TLM_COMPLETED;
    }
};

class ifu_testbench : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(ifu_testbench);

    e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t>& pipe;

    explicit ifu_testbench(sc_core::sc_module_name name,
                           e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t>& pipeline)
        : sc_core::sc_module(name), pipe(pipeline)
    {
        SC_THREAD(run);
    }

private:
    e203sim::ifu_exu_packet wait_valid_packet()
    {
        unsigned int cycles = 0;
        while (!pipe.valid()) {
            wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
            ++cycles;
            expect(cycles < 100u, "timed out waiting for pipeline valid");
        }
        wait(sc_core::SC_ZERO_TIME);
        return pipe.read();
    }

    e203sim::ifu_exu_packet accept_packet()
    {
        auto packet = wait_valid_packet();
        pipe.set_ready(true);
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::SC_ZERO_TIME);
        pipe.set_ready(false);
        wait(sc_core::SC_ZERO_TIME);
        return packet;
    }

    e203sim::ifu_exu_packet wait_packet_at(uint32_t pc)
    {
        for (unsigned int cycles = 0; cycles < 100u; ++cycles) {
            if (pipe.valid()) {
                const auto packet = pipe.read();
                if (packet.pc == pc) {
                    return packet;
                }
            }
            wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
            wait(sc_core::SC_ZERO_TIME);
        }
        std::ostringstream oss;
        oss << "timed out waiting for expected pipeline pc=0x" << std::hex << pc;
        fail(oss.str());
    }

    e203sim::ifu_exu_packet accept_packet_at(uint32_t pc)
    {
        auto packet = wait_packet_at(pc);
        pipe.set_ready(true);
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::SC_ZERO_TIME);
        pipe.set_ready(false);
        wait(sc_core::SC_ZERO_TIME);
        return packet;
    }

    void expect_packet(const e203sim::ifu_exu_packet& packet,
                       uint32_t pc,
                       uint32_t instr,
                       bool instr_32bit,
                       const std::string& label)
    {
        std::ostringstream oss;
        oss << label << ": expected pc=0x" << std::hex << pc
            << " instr=0x" << instr << ", actual " << packet;
        const std::string detail = oss.str();
        expect(packet.pc == pc, detail + ": pc mismatch");
        expect(packet.instr == instr, detail + ": instr mismatch");
        expect(packet.instr_32bit == instr_32bit, label + ": instr length mismatch");
        expect(packet.pc_valid, label + ": pc should be valid");
        expect(!packet.bus_error, label + ": should not report bus error");
        expect(!packet.misalign, label + ": should not report misalign");
    }

    void run()
    {
        pipe.set_ready(false);
        wait(sc_core::SC_ZERO_TIME);

        auto packet = accept_packet_at(kItcmBase);
        expect_packet(packet, kItcmBase, kCnop, false, "first 16-bit instruction");

        pipe.set_ready(false);
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
        wait(sc_core::SC_ZERO_TIME);
        packet = wait_packet_at(kItcmBase + 6u);
        expect_packet(packet, kItcmBase + 6u, kAddiX1, true, "cross-lane 32-bit instruction");
        wait(sc_core::sc_time(2 * kCycleNs, sc_core::SC_NS));
        expect(pipe.valid(), "backpressure should hold valid high");
        expect(pipe.read() == packet, "backpressure should hold packet stable");

        pipe.set_ready(true);
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));

        pipe.set_ready(false);
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
        wait(sc_core::SC_ZERO_TIME);
        packet = wait_packet_at(kItcmBase + 10u);
        expect_packet(packet, kItcmBase + 10u, kCnop, false, "16-bit instruction after cross-lane fetch");
        packet = accept_packet_at(kItcmBase + 10u);
        expect_packet(packet, kItcmBase + 10u, kCnop, false, "accept 16-bit instruction after cross-lane fetch");

        pipe.set_ready(false);
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
        wait(sc_core::SC_ZERO_TIME);
        packet = wait_packet_at(kItcmBase + 8u);
        expect(packet.pc == kItcmBase + 8u, "predicted branch target pc mismatch");

        pipe.pulse_flush(kItcmBase + 2u);
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
        wait(sc_core::SC_ZERO_TIME);
        packet = wait_packet_at(kItcmBase + 2u);
        expect_packet(packet, kItcmBase + 2u, kAddiX0, true, "flush should refetch target packet");

        pipe.set_ready(false);
        packet = accept_packet_at(kItcmBase + 2u);
        expect_packet(packet, kItcmBase + 2u, kAddiX0, true, "accept refetched flush target");

        pipe.set_ready(false);
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
        wait(sc_core::SC_ZERO_TIME);
        packet = wait_packet_at(kItcmBase + 6u);
        expect_packet(packet, kItcmBase + 6u, kAddiX1, true, "held packet before invalid-address flush");

        pipe.pulse_flush(kItcmBase + kItcmSize);
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
        wait(sc_core::SC_ZERO_TIME);
        packet = wait_packet_at(kItcmBase + kItcmSize);
        expect(packet.pc == kItcmBase + kItcmSize, "invalid-address flush should refetch target pc");
        pipe.set_ready(false);

        packet = accept_packet_at(kItcmBase + kItcmSize);
        expect(packet.pc == kItcmBase + kItcmSize, "invalid fetch pc mismatch");
        expect(packet.bus_error, "invalid fetch should report bus error");
        expect(!packet.pc_valid, "invalid fetch should clear pc_valid");

        std::cout << "[TEST][PASS] ifu_ca component test passed" << std::endl;
        sc_core::sc_stop();
    }
};

} // namespace

int sc_main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    e203sim::memory_config itcm_cfg;
    itcm_cfg.name = "itcm";
    itcm_cfg.base_addr = kItcmBase;
    itcm_cfg.size = kItcmSize;
    itcm_cfg.read_latency_cycles = 1;
    itcm_cfg.write_latency_cycles = 1;

    e203sim::sim_config sim_cfg{};
    sim_cfg.cycle_ns = kCycleNs;
    sim_cfg.enable_debug = false;
    sim_cfg.itcm = &itcm_cfg;

    e203sim::debug_logger::instance().disable();

    itcm_ca itcm("itcm", &itcm_cfg, kCycleNs);
    e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t> pipe("ifu_exu_pipe");
    ifu_ca ifu("ifu", sim_cfg);
    dummy_initiator unused_lsu("unused_lsu");
    ifu.bind_pipeline(pipe);
    ifu.ifu2itcm_initiator_socket.bind(itcm.ifu2itcm_target_socket);
    unused_lsu.socket.bind(itcm.lsu2itcm_target_socket);

    const uint32_t branch_back = encode_beq(0, 0, -4);
    const uint64_t lane0 = 0x0093000000130001ull;
    const uint64_t lane1 = (static_cast<uint64_t>(branch_back) << 32u) | 0x00010010ull;
    expect(itcm.sram_write(0, lane0, 0xffu), "failed to preload ITCM lane0");
    expect(itcm.sram_write(8, lane1, 0xffu), "failed to preload ITCM lane1");

    ifu_testbench testbench("ifu_testbench", pipe);

    sc_core::sc_start();
    return 0;
}
