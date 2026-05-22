#include "common/debug_logger.h"
#include "models/bus/biu_ca.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

namespace {

constexpr uint32_t kCycleNs = 10u;

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

struct trans_ctx {
    tlm::tlm_generic_payload trans;
    std::array<uint8_t, 4> data{};
    bool done = false;
    sc_core::sc_event done_event;
};

class blocking_target : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<blocking_target> socket;
    unsigned int access_count = 0;

    explicit blocking_target(sc_core::sc_module_name name)
        : sc_core::sc_module(name), socket("socket")
    {
        socket.register_b_transport(this, &blocking_target::b_transport);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        ++access_count;
        delay += sc_core::sc_time(kCycleNs, sc_core::SC_NS);
        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            auto* data = trans.get_data_ptr();
            data[0] = 0x78;
            data[1] = 0x56;
            data[2] = 0x34;
            data[3] = 0x12;
        }
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

class nb_initiator : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(nb_initiator);

    tlm_utils::simple_initiator_socket<nb_initiator> socket;
    blocking_target& target;

    nb_initiator(sc_core::sc_module_name name, blocking_target& target_ref)
        : sc_core::sc_module(name), socket("socket"), target(target_ref)
    {
        socket.register_nb_transport_bw(this, &nb_initiator::nb_transport_bw);
        SC_THREAD(run);
    }

    tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay)
    {
        (void)delay;
        expect(outstanding_ != nullptr, "BIU returned response without outstanding");
        expect(&trans == &outstanding_->trans, "BIU response payload mismatch");
        expect(phase == tlm::BEGIN_RESP, "BIU should return BEGIN_RESP");
        outstanding_->done = true;
        phase = tlm::END_RESP;
        outstanding_->done_event.notify(sc_core::SC_ZERO_TIME);
        return tlm::TLM_COMPLETED;
    }

private:
    std::unique_ptr<trans_ctx> outstanding_;

    static std::unique_ptr<trans_ctx> make_read(uint32_t addr)
    {
        auto ctx = std::make_unique<trans_ctx>();
        ctx->trans.set_command(tlm::TLM_READ_COMMAND);
        ctx->trans.set_address(addr);
        ctx->trans.set_data_ptr(ctx->data.data());
        ctx->trans.set_data_length(4);
        ctx->trans.set_streaming_width(4);
        ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        return ctx;
    }

    void run()
    {
        outstanding_ = make_read(0x20000000u);
        tlm::tlm_phase phase = tlm::BEGIN_REQ;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        const sc_core::sc_time begin = sc_core::sc_time_stamp();
        const tlm::tlm_sync_enum res = socket->nb_transport_fw(outstanding_->trans, phase, delay);

        expect(res == tlm::TLM_UPDATED, "BIU should accept BEGIN_REQ with TLM_UPDATED");
        expect(phase == tlm::END_REQ, "BIU should return END_REQ after buffering request");
        expect(delay == sc_core::SC_ZERO_TIME, "BIU should consume request with zero returned delay");

        auto second = make_read(0x20000004u);
        tlm::tlm_phase second_phase = tlm::BEGIN_REQ;
        sc_core::sc_time second_delay = sc_core::SC_ZERO_TIME;
        const tlm::tlm_sync_enum second_res =
            socket->nb_transport_fw(second->trans, second_phase, second_delay);
        expect(second_res == tlm::TLM_ACCEPTED, "BIU should reject request while buffer is active");
        expect(second_phase == tlm::BEGIN_REQ, "rejected request should keep BEGIN_REQ phase");

        wait(outstanding_->done_event);
        expect(outstanding_->done, "BIU response did not complete");
        expect(sc_core::sc_time_stamp() == begin + sc_core::sc_time(kCycleNs, sc_core::SC_NS),
               "BIU should return response after blocking target annotated delay");
        expect(outstanding_->trans.get_response_status() == tlm::TLM_OK_RESPONSE,
               "BIU should preserve blocking target response status");
        expect(outstanding_->data == std::array<uint8_t, 4>{0x78, 0x56, 0x34, 0x12},
               "BIU should preserve read data from blocking target");
        expect(target.access_count == 1, "blocking target should see one accepted access");

        std::cout << "[TEST][PASS] biu_ca component test passed" << std::endl;
        sc_core::sc_stop();
    }
};

} // namespace

int sc_main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    e203sim::debug_logger::instance().disable();

    biu_ca biu("biu");
    blocking_target target("target");
    nb_initiator initiator("initiator", target);

    initiator.socket.bind(biu.lsu2biu_target_socket);
    biu.biu2router_initiator_socket.bind(target.socket);

    sc_core::sc_start();
    return 0;
}
