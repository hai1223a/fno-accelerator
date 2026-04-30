#include "common/debug_logger.h"
#include "common/types.h"
#include "models/memory/itcm_ca.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <tlm_utils/simple_initiator_socket.h>

namespace {

constexpr uint32_t kItcmBase = 0x80000000u;
constexpr uint32_t kItcmSize = 0x10000u;
constexpr uint32_t kCycleNs = 10u;

uint32_t load_le32(const std::array<uint8_t, 8>& data)
{
    return (static_cast<uint32_t>(data[0]) << 0) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t load_le64(const std::array<uint8_t, 8>& data)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        value |= static_cast<uint64_t>(data[i]) << (i * 8);
    }
    return value;
}

void store_le32(std::array<uint8_t, 8>& data, uint32_t value)
{
    data[0] = static_cast<uint8_t>((value >> 0) & 0xff);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xff);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xff);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

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

struct TransCtx {
    tlm::tlm_generic_payload trans;
    std::array<uint8_t, 8> data{};
    std::array<uint8_t, 8> byte_enable{};
    tlm::tlm_response_status expected_status = tlm::TLM_OK_RESPONSE;
    bool done = false;
    sc_core::sc_time begin_time;
    sc_core::sc_time response_time;
    sc_core::sc_event response_event;
};

class itcm_test_initiator : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(itcm_test_initiator);

    tlm_utils::simple_initiator_socket<itcm_test_initiator, 64> socket;

    explicit itcm_test_initiator(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
    {
        socket.register_nb_transport_bw(this, &itcm_test_initiator::nb_transport_bw);
    }

    tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay)
    {
        (void)delay;
        expect(outstanding_ != nullptr, "received response without outstanding transaction");
        expect(&trans == &outstanding_->trans, "response payload does not match outstanding payload");
        expect(phase == tlm::BEGIN_RESP, "expected BEGIN_RESP from ITCM");
        expect(trans.get_response_status() == outstanding_->expected_status,
               "ITCM response status mismatch");

        outstanding_->done = true;
        outstanding_->response_time = sc_core::sc_time_stamp();
        phase = tlm::END_RESP;
        outstanding_->response_event.notify(sc_core::SC_ZERO_TIME);
        return tlm::TLM_COMPLETED;
    }

    std::unique_ptr<TransCtx> issue(std::unique_ptr<TransCtx> ctx)
    {
        expect(outstanding_ == nullptr, "initiator attempted overlapping transaction");
        tlm::tlm_phase phase = tlm::BEGIN_REQ;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        ctx->begin_time = sc_core::sc_time_stamp();
        outstanding_ = std::move(ctx);

        const tlm::tlm_sync_enum res = socket->nb_transport_fw(outstanding_->trans, phase, delay);
        expect(res == tlm::TLM_UPDATED, "ITCM should return TLM_UPDATED for accepted BEGIN_REQ");
        expect(phase == tlm::END_REQ, "ITCM should return END_REQ for accepted BEGIN_REQ");
        expect(delay == sc_core::SC_ZERO_TIME, "ITCM should consume request with zero returned delay");

        wait(outstanding_->response_event);
        expect(outstanding_->done, "transaction did not complete");

        auto result = std::move(outstanding_);
        return result;
    }

    void issue_async(std::unique_ptr<TransCtx> ctx)
    {
        expect(outstanding_ == nullptr, "initiator attempted overlapping async transaction");
        tlm::tlm_phase phase = tlm::BEGIN_REQ;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        ctx->begin_time = sc_core::sc_time_stamp();
        outstanding_ = std::move(ctx);

        const tlm::tlm_sync_enum res = socket->nb_transport_fw(outstanding_->trans, phase, delay);
        expect(res == tlm::TLM_UPDATED, "ITCM should accept async BEGIN_REQ");
        expect(phase == tlm::END_REQ, "accepted async request should return END_REQ");
        expect(delay == sc_core::SC_ZERO_TIME, "accepted async request should return zero delay");
    }

    std::unique_ptr<TransCtx> take_completed()
    {
        wait(outstanding_->response_event);
        expect(outstanding_->done, "async transaction did not complete");
        auto result = std::move(outstanding_);
        return result;
    }

private:
    std::unique_ptr<TransCtx> outstanding_;
};

std::unique_ptr<TransCtx> make_lsu_write(uint32_t addr,
                                         uint32_t value,
                                         std::array<uint8_t, 8> byte_enable)
{
    auto ctx = std::make_unique<TransCtx>();
    store_le32(ctx->data, value);
    ctx->byte_enable = byte_enable;
    ctx->trans.set_command(tlm::TLM_WRITE_COMMAND);
    ctx->trans.set_address(addr);
    ctx->trans.set_data_ptr(ctx->data.data());
    ctx->trans.set_data_length(4);
    ctx->trans.set_byte_enable_ptr(ctx->byte_enable.data());
    ctx->trans.set_byte_enable_length(4);
    ctx->trans.set_streaming_width(4);
    ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return ctx;
}

std::unique_ptr<TransCtx> make_lsu_read(uint32_t addr,
                                        tlm::tlm_response_status expected_status = tlm::TLM_OK_RESPONSE)
{
    auto ctx = std::make_unique<TransCtx>();
    ctx->expected_status = expected_status;
    ctx->data = {0xde, 0xad, 0xbe, 0xef, 0xaa, 0xbb, 0xcc, 0xdd};
    ctx->trans.set_command(tlm::TLM_READ_COMMAND);
    ctx->trans.set_address(addr);
    ctx->trans.set_data_ptr(ctx->data.data());
    ctx->trans.set_data_length(4);
    ctx->trans.set_streaming_width(4);
    ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return ctx;
}

std::unique_ptr<TransCtx> make_ifu_read(uint32_t addr,
                                        tlm::tlm_response_status expected_status = tlm::TLM_OK_RESPONSE)
{
    auto ctx = std::make_unique<TransCtx>();
    ctx->expected_status = expected_status;
    ctx->data = {0xde, 0xad, 0xbe, 0xef, 0xaa, 0xbb, 0xcc, 0xdd};
    ctx->trans.set_command(tlm::TLM_READ_COMMAND);
    ctx->trans.set_address(addr);
    ctx->trans.set_data_ptr(ctx->data.data());
    ctx->trans.set_data_length(8);
    ctx->trans.set_streaming_width(8);
    ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return ctx;
}

class itcm_testbench : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(itcm_testbench);

    itcm_test_initiator lsu;
    itcm_test_initiator ifu;

    explicit itcm_testbench(sc_core::sc_module_name name)
        : sc_core::sc_module(name), lsu("lsu_initiator"), ifu("ifu_initiator")
    {
        SC_THREAD(run);
    }

private:
    void run()
    {
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));

        const auto low_write = lsu.issue(make_lsu_write(kItcmBase, 0x11223344u, {1, 1, 1, 1, 0, 0, 0, 0}));
        expect(low_write->response_time == low_write->begin_time + sc_core::sc_time(kCycleNs, sc_core::SC_NS),
               "LSU low word write should complete after one ITCM cycle");

        lsu.issue(make_lsu_write(kItcmBase + 4, 0xaabbccddu, {1, 1, 1, 1, 0, 0, 0, 0}));
        auto lane = ifu.issue(make_ifu_read(kItcmBase));
        expect(load_le64(lane->data) == 0xaabbccdd11223344ull,
               "IFU 64-bit lane read mismatch after LSU low/high writes");

        auto high_word = lsu.issue(make_lsu_read(kItcmBase + 4));
        expect(load_le32(high_word->data) == 0xaabbccddu,
               "LSU high 32-bit read should extract upper half of ITCM lane");
        expect(high_word->data[4] == 0xaa && high_word->data[5] == 0xbb,
               "LSU 32-bit read must not overwrite bytes outside data_length");

        lsu.issue(make_lsu_write(kItcmBase + 6, 0xeeeeeeeeu, {0, 0, 1, 0, 0, 0, 0, 0}));
        high_word = lsu.issue(make_lsu_read(kItcmBase + 4));
        expect(load_le32(high_word->data) == 0xaaeeccddu,
               "LSU byte-enable write should shift into the upper 64-bit lane half");

        ifu.issue_async(make_ifu_read(kItcmBase));
        lsu.issue_async(make_lsu_read(kItcmBase + 4));
        auto lsu_result = lsu.take_completed();
        auto ifu_result = ifu.take_completed();
        expect(lsu_result->response_time == lsu_result->begin_time + sc_core::sc_time(kCycleNs, sc_core::SC_NS),
               "LSU should win same-timestamp ITCM arbitration");
        expect(ifu_result->response_time == ifu_result->begin_time + sc_core::sc_time(2 * kCycleNs, sc_core::SC_NS),
               "IFU should be served one cycle after same-timestamp LSU request");
        expect(load_le32(lsu_result->data) == 0xaaeeccddu,
               "arbitrated LSU readback mismatch");
        expect(load_le64(ifu_result->data) == 0xaaeeccdd11223344ull,
               "arbitrated IFU lane readback mismatch");

        lsu.issue(make_lsu_write(kItcmBase + kItcmSize - 8, 0x55667788u, {1, 1, 1, 1, 0, 0, 0, 0}));
        lsu.issue(make_lsu_write(kItcmBase + kItcmSize - 4, 0x99aabbccu, {1, 1, 1, 1, 0, 0, 0, 0}));
        auto last_lane = ifu.issue(make_ifu_read(kItcmBase + kItcmSize - 8));
        expect(load_le64(last_lane->data) == 0x99aabbcc55667788ull,
               "IFU should read the final valid 64-bit ITCM lane");

        auto last_high_word = lsu.issue(make_lsu_read(kItcmBase + kItcmSize - 4));
        expect(load_le32(last_high_word->data) == 0x99aabbccu,
               "LSU should read the final valid high 32-bit ITCM word");

        auto below_base = lsu.issue(make_lsu_read(kItcmBase - 4, tlm::TLM_ADDRESS_ERROR_RESPONSE));
        expect(below_base->response_time == below_base->begin_time + sc_core::sc_time(kCycleNs, sc_core::SC_NS),
               "invalid low address should still return through the normal response path");
        expect(below_base->data[0] == 0xde && below_base->data[1] == 0xad,
               "invalid low address should not modify payload data");

        auto at_end = ifu.issue(make_ifu_read(kItcmBase + kItcmSize, tlm::TLM_ADDRESS_ERROR_RESPONSE));
        expect(at_end->response_time == at_end->begin_time + sc_core::sc_time(kCycleNs, sc_core::SC_NS),
               "invalid high address should still return through the normal response path");
        expect(load_le64(at_end->data) == 0xddccbbaaefbeaddeull,
               "invalid high address should not modify IFU payload data");

        std::cout << "[TEST][PASS] itcm_ca component test passed" << std::endl;
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

    e203sim::debug_logger::instance().disable();

    itcm_ca itcm("itcm", &itcm_cfg, kCycleNs);
    itcm_testbench testbench("itcm_testbench");
    testbench.lsu.socket.bind(itcm.lsu2itcm_target_socket);
    testbench.ifu.socket.bind(itcm.ifu2itcm_target_socket);

    sc_core::sc_start();
    return 0;
}
