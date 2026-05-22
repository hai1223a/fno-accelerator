#include "common/pipeline_if.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <string>
#include <systemc>

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

class source_stage : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(source_stage);

    sc_core::sc_port<e203sim::pipeline_producer_if<uint32_t, uint32_t>> out_port;

    explicit source_stage(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
    {
        SC_THREAD(thread);
    }

private:
    std::queue<uint32_t> queue_;
    uint32_t pc_ = 0;
    uint64_t last_flush_epoch_ = 0;
    sc_core::sc_time clk_ = sc_core::sc_time(kCycleNs, sc_core::SC_NS);

    void clear_pipeline_output()
    {
        out_port->set_valid(false);
        out_port->write(0);
    }

    void thread()
    {
        clear_pipeline_output();
        wait(sc_core::SC_ZERO_TIME);

        while (true) {
            wait(clk_, out_port->flush_event());
            if (out_port->flush_epoch() != last_flush_epoch_) {
                last_flush_epoch_ = out_port->flush_epoch();
                clear_pipeline_output();
                while (!queue_.empty()) {
                    queue_.pop();
                }
                pc_ = out_port->flush_target();
                continue;
            }

            if (out_port->fire()) {
                queue_.pop();
                out_port->set_valid(false);
            }
            if (queue_.empty()) {
                pc_ += 4;
                queue_.push(pc_);
            }
            if (!queue_.empty()) {
                out_port->set_valid(true);
                out_port->write(queue_.front());
            }
        }
    }
};

class middle_stage : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(middle_stage);

    sc_core::sc_port<e203sim::pipeline_consumer_if<uint32_t, uint32_t>> in_port;
    sc_core::sc_port<e203sim::pipeline_producer_if<uint32_t, uint32_t>> out_port;

    explicit middle_stage(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
    {
        SC_THREAD(thread);
    }

private:
    std::queue<uint32_t> queue_;
    uint32_t pc_ = 0;
    uint64_t last_flush_epoch_ = 0;
    bool busy_ = false;
    sc_core::sc_time clk_ = sc_core::sc_time(kCycleNs, sc_core::SC_NS);

    void clear_pipeline_output()
    {
        out_port->set_valid(false);
        out_port->write(0);
    }

    void process_receive_data()
    {
        busy_ = false;
        pc_ += 8;
        queue_.push(pc_);
    }

    void thread()
    {
        in_port->set_ready(true);
        clear_pipeline_output();
        wait(sc_core::SC_ZERO_TIME);

        while (true) {
            wait(clk_, out_port->flush_event());

            if (out_port->flush_epoch() != last_flush_epoch_) {
                last_flush_epoch_ = out_port->flush_epoch();
                clear_pipeline_output();
                while (!queue_.empty()) {
                    queue_.pop();
                }
                in_port->pulse_flush(out_port->flush_target());
                continue;
            }

            if (out_port->fire()) {
                queue_.pop();
                out_port->set_valid(false);
            }
            if (in_port->fire()) {
                busy_ = true;
                pc_ = in_port->read();
                in_port->set_ready(false);
            }
            if (busy_) {
                process_receive_data();
            }
            if (!busy_) {
                in_port->set_ready(true);
            }
            if (!queue_.empty()) {
                out_port->set_valid(true);
                out_port->write(queue_.front());
            }
        }
    }
};

class sink_stage : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(sink_stage);

    sc_core::sc_port<e203sim::pipeline_consumer_if<uint32_t, uint32_t>> in_port;
    bool received = false;
    uint32_t received_value = 0;

    explicit sink_stage(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
    {
        SC_THREAD(thread);
    }

private:
    sc_core::sc_time clk_ = sc_core::sc_time(kCycleNs, sc_core::SC_NS);

    void thread()
    {
        in_port->set_ready(true);
        wait(sc_core::SC_ZERO_TIME);

        while (true) {
            wait(clk_);
            if (in_port->fire()) {
                received = true;
                received_value = in_port->read();
                in_port->set_ready(false);
            }
            if (received) {
                in_port->set_ready(true);
            }
        }
    }
};

class pipeline_stage_order_testbench : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(pipeline_stage_order_testbench);

    e203sim::pipeline_channel<uint32_t, uint32_t>& a2b;
    e203sim::pipeline_channel<uint32_t, uint32_t>& b2c;
    sink_stage& sink;

    pipeline_stage_order_testbench(sc_core::sc_module_name name,
                                   e203sim::pipeline_channel<uint32_t, uint32_t>& a2b_ref,
                                   e203sim::pipeline_channel<uint32_t, uint32_t>& b2c_ref,
                                   sink_stage& sink_ref)
        : sc_core::sc_module(name), a2b(a2b_ref), b2c(b2c_ref), sink(sink_ref)
    {
        SC_THREAD(run);
    }

private:
    void tick(unsigned int count = 1)
    {
        for (unsigned int i = 0; i < count; ++i) {
            wait(sc_core::sc_time(kCycleNs, sc_core::SC_NS));
            wait(sc_core::SC_ZERO_TIME);
            wait(sc_core::SC_ZERO_TIME);
        }
    }

    void run()
    {
        tick();
        expect(a2b.valid(), "source should drive first A2B packet after one cycle");
        expect(a2b.read() == 4u, "unexpected first A2B payload");
        expect(!b2c.valid(), "middle output should still be empty before A2B fire");

        tick();
        expect(b2c.valid(), "middle should drive B2C in the same cycle it accepts A2B");
        expect(b2c.read() == 12u, "middle should transform 4 into 12");

        tick();
        expect(sink.received, "sink should receive middle output on the following cycle");
        expect(sink.received_value == 12u, "sink received payload mismatch");

        std::cout << "[TEST][PASS] pipeline_stage_order component test passed" << std::endl;
        sc_core::sc_stop();
    }
};

} // namespace

int sc_main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    e203sim::pipeline_channel<uint32_t, uint32_t> a2b("a2b");
    e203sim::pipeline_channel<uint32_t, uint32_t> b2c("b2c");
    source_stage source("source");
    middle_stage middle("middle");
    sink_stage sink("sink");

    source.out_port.bind(a2b);
    middle.in_port.bind(a2b);
    middle.out_port.bind(b2c);
    sink.in_port.bind(b2c);

    pipeline_stage_order_testbench testbench("testbench", a2b, b2c, sink);
    sc_core::sc_start();
    return 0;
}
