#include "common/debug_logger.h"
#include "common/types.h"
#include "models/memory/dtcm_ca.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <tlm_utils/simple_initiator_socket.h>

namespace {

constexpr uint32_t kDtcmBase = 0x90000000u;
constexpr uint32_t kDtcmSize = 0x10000u;
constexpr uint32_t kCycleNs = 10u;

uint32_t load_le32(const std::array<uint8_t, 4>& data)
{
    return (static_cast<uint32_t>(data[0]) << 0) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void store_le32(std::array<uint8_t, 4>& data, uint32_t value)
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
    std::array<uint8_t, 4> data{};
    std::array<uint8_t, 4> byte_enable{};
    uint32_t addr = 0;
    bool done = false;
    sc_core::sc_time begin_time;
    sc_core::sc_time response_time;
    sc_core::sc_event response_event;
};

class dtcm_test_initiator : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(dtcm_test_initiator);

    tlm_utils::simple_initiator_socket<dtcm_test_initiator> socket;

    explicit dtcm_test_initiator(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
    {
        socket.register_nb_transport_bw(this, &dtcm_test_initiator::nb_transport_bw);
        SC_THREAD(run);
    }

    tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay)
    {
        (void)delay;
        expect(outstanding_ != nullptr, "received response without outstanding transaction");
        expect(&trans == &outstanding_->trans, "response payload does not match outstanding payload");
        expect(phase == tlm::BEGIN_RESP, "expected BEGIN_RESP from DTCM");
        expect(!trans.is_response_error(), "DTCM returned response error");

        outstanding_->done = true;
        outstanding_->response_time = sc_core::sc_time_stamp();
        phase = tlm::END_RESP;
        outstanding_->response_event.notify(sc_core::SC_ZERO_TIME);
        return tlm::TLM_COMPLETED;
    }

private:
    std::unique_ptr<TransCtx> outstanding_;

    static std::unique_ptr<TransCtx> make_write(uint32_t addr,
                                                uint32_t value,
                                                std::array<uint8_t, 4> byte_enable)
    {
        auto ctx = std::make_unique<TransCtx>();
        ctx->addr = addr;
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

    static std::unique_ptr<TransCtx> make_read(uint32_t addr, unsigned int data_length = 4)
    {
        auto ctx = std::make_unique<TransCtx>();
        ctx->addr = addr;
        ctx->data = {0xde, 0xad, 0xbe, 0xef};
        ctx->trans.set_command(tlm::TLM_READ_COMMAND);
        ctx->trans.set_address(addr);
        ctx->trans.set_data_ptr(ctx->data.data());
        ctx->trans.set_data_length(data_length);
        ctx->trans.set_streaming_width(4);
        ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        return ctx;
    }

    std::unique_ptr<TransCtx> run_transaction(std::unique_ptr<TransCtx> ctx)
    {
        expect(outstanding_ == nullptr, "testbench attempted overlapping normal transaction");

        tlm::tlm_phase phase = tlm::BEGIN_REQ;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        ctx->begin_time = sc_core::sc_time_stamp();
        outstanding_ = std::move(ctx);

        const tlm::tlm_sync_enum res =
            socket->nb_transport_fw(outstanding_->trans, phase, delay);
        expect(res == tlm::TLM_UPDATED, "DTCM should return TLM_UPDATED for accepted BEGIN_REQ");
        expect(phase == tlm::END_REQ, "DTCM should return END_REQ for accepted BEGIN_REQ");
        expect(delay == sc_core::SC_ZERO_TIME, "DTCM should consume request with zero returned delay");

        wait(outstanding_->response_event);
        expect(outstanding_->done, "transaction did not complete");
        expect(outstanding_->response_time == outstanding_->begin_time + sc_core::sc_time(kCycleNs, sc_core::SC_NS),
               "DTCM response should arrive one cycle after BEGIN_REQ");
        expect(outstanding_->trans.get_response_status() == tlm::TLM_OK_RESPONSE,
               "DTCM should set TLM_OK_RESPONSE");

        auto result = std::move(outstanding_);
        return result;
    }

    void test_busy_rejects_second_request()
    {
        auto first = make_read(kDtcmBase);
        auto second = make_read(kDtcmBase + 4);

        tlm::tlm_phase first_phase = tlm::BEGIN_REQ;
        sc_core::sc_time first_delay = sc_core::SC_ZERO_TIME;
        outstanding_ = std::move(first);
        const tlm::tlm_sync_enum first_res =
            socket->nb_transport_fw(outstanding_->trans, first_phase, first_delay);
        expect(first_res == tlm::TLM_UPDATED && first_phase == tlm::END_REQ,
               "first request in busy test should be accepted");

        tlm::tlm_phase second_phase = tlm::BEGIN_REQ;
        sc_core::sc_time second_delay = sc_core::SC_ZERO_TIME;
        const tlm::tlm_sync_enum second_res =
            socket->nb_transport_fw(second->trans, second_phase, second_delay);
        expect(second_res == tlm::TLM_ACCEPTED,
               "DTCM should not accept a second request while pending slot is busy");
        expect(second_phase == tlm::BEGIN_REQ,
               "rejected request should keep BEGIN_REQ phase for initiator retry");

        wait(outstanding_->response_event);
        outstanding_.reset();
    }

    void test_byte_enable_variants()
    {
        run_transaction(make_write(kDtcmBase + 0x20, 0x55667788u, {1, 1, 1, 1}));
        run_transaction(make_write(kDtcmBase + 0x20, 0xaaaabbbbu, {1, 1, 0, 0}));
        auto low_half = run_transaction(make_read(kDtcmBase + 0x20));
        expect(load_le32(low_half->data) == 0x5566bbbbu, "low halfword byte-enable write mismatch");

        run_transaction(make_write(kDtcmBase + 0x23, 0xccccccccu, {0, 0, 0, 1}));
        auto high_byte = run_transaction(make_read(kDtcmBase + 0x20));
        expect(load_le32(high_byte->data) == 0xcc66bbbbu, "high byte byte-enable write mismatch");

        run_transaction(make_write(kDtcmBase + 0x20, 0xffffffffu, {0, 0, 0, 0}));
        auto no_write = run_transaction(make_read(kDtcmBase + 0x20));
        expect(load_le32(no_write->data) == 0xcc66bbbbu, "zero byte-enable write should not change DTCM lane");
    }

    void test_read_data_length()
    {
        run_transaction(make_write(kDtcmBase + 0x30, 0x12345678u, {1, 1, 1, 1}));
        auto short_read = run_transaction(make_read(kDtcmBase + 0x30, 2));

        expect(short_read->data[0] == 0x78, "short read byte 0 mismatch");
        expect(short_read->data[1] == 0x56, "short read byte 1 mismatch");
        expect(short_read->data[2] == 0xbe, "short read should not overwrite byte 2");
        expect(short_read->data[3] == 0xef, "short read should not overwrite byte 3");
    }

    void test_end_resp_forward_path()
    {
        auto ctx = make_read(kDtcmBase);
        tlm::tlm_phase phase = tlm::END_RESP;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        const tlm::tlm_sync_enum res = socket->nb_transport_fw(ctx->trans, phase, delay);
        expect(res == tlm::TLM_COMPLETED, "DTCM should complete END_RESP on forward path");
    }

    void run()
    {
        wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));

        run_transaction(make_write(kDtcmBase, 0x11223344u, {1, 1, 1, 1}));
        auto word = run_transaction(make_read(kDtcmBase));
        expect(load_le32(word->data) == 0x11223344u, "word readback mismatch");

        run_transaction(make_write(kDtcmBase + 2, 0xaabbaabbu, {0, 0, 1, 1}));
        auto half = run_transaction(make_read(kDtcmBase));
        expect(load_le32(half->data) == 0xaabb3344u, "halfword high write readback mismatch");

        run_transaction(make_write(kDtcmBase + 1, 0xccccccccu, {0, 1, 0, 0}));
        auto byte = run_transaction(make_read(kDtcmBase));
        expect(load_le32(byte->data) == 0xaabbcc44u, "byte middle write readback mismatch");

        test_byte_enable_variants();
        test_read_data_length();
        test_busy_rejects_second_request();
        test_end_resp_forward_path();

        std::cout << "[TEST][PASS] dtcm_ca component test passed" << std::endl;
        sc_core::sc_stop();
    }
};

} // namespace

int sc_main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    e203sim::memory_config dtcm_cfg;
    dtcm_cfg.name = "dtcm";
    dtcm_cfg.base_addr = kDtcmBase;
    dtcm_cfg.size = kDtcmSize;
    dtcm_cfg.read_latency_cycles = 1;
    dtcm_cfg.write_latency_cycles = 1;

    e203sim::debug_logger::instance().disable();

    dtcm_ca dtcm("dtcm", &dtcm_cfg, kCycleNs);
    dtcm_test_initiator initiator("dtcm_test_initiator");
    initiator.socket.bind(dtcm.lsu2dtcm_target_socket);

    sc_core::sc_start();
    return 0;
}
