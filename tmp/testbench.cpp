#include "common/pipeline_if.h"
#include "sim/thread_trace.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <systemc>
#include <queue>

class A : public sc_core::sc_module {
public:
    uint32_t pc = 0;
    int delay = 0;
    uint64_t last_flush_epoch = 0;
    sc_core::sc_time clk = sc_core::sc_time(10, sc_core::SC_NS);
    SC_HAS_PROCESS(A);

    sc_core::sc_port<e203sim::pipeline_producer_if<uint32_t, uint32_t>> A2B_pipe_port;
    std::queue<uint32_t> local_queue;

    A(sc_core::sc_module_name module_name): sc_core::sc_module(module_name) {
        SC_THREAD(produce_thread);
        std::cout << module_name << " created !" << std::endl;
    }
    ~A() {};
    void clear_pipeline_output()
    {
        // 清输出时同时写一个空 packet，避免后级误读旧数据。
        A2B_pipe_port->set_valid(false);
        A2B_pipe_port->write(0);
        THREAD_TRACE("A生产线程", "清空输出", "valid=0 payload=0");
    }
    void process_new_data()
    {
        if (delay == 0)
        {
            pc += 4;
            local_queue.push(pc);
            THREAD_TRACE("A生产线程", "生成数据",
                         "pc=0x" << std::hex << pc << std::dec
                                  << " 队列深度=" << local_queue.size());
            delay = rand()%3;
            THREAD_TRACE("A生产线程", "设置生成延迟", "delay=" << delay);
        } else {
            THREAD_TRACE("A生产线程", "生成等待",
                         "delay_before=" << delay
                                         << " 队列深度=" << local_queue.size());
            delay--;
        }

    }
    void produce_thread(){
        clear_pipeline_output();
        sc_core::wait(sc_core::SC_ZERO_TIME);
        while (true)
        {
            sc_core::wait(clk, A2B_pipe_port->flush_event());
            THREAD_TRACE("A生产线程", "线程唤醒",
                         "ready=" << A2B_pipe_port->ready()
                                  << " fire=" << A2B_pipe_port->fire()
                                  << " flush_epoch=" << A2B_pipe_port->flush_epoch()
                                  << " 队列深度=" << local_queue.size()
                                  << " pc=0x" << std::hex << pc << std::dec
                                  << " delay=" << delay);

            if(A2B_pipe_port->flush_epoch() != last_flush_epoch) {
                last_flush_epoch = A2B_pipe_port->flush_epoch();
                const uint32_t flush_target = A2B_pipe_port->flush_target();
                const size_t old_depth = local_queue.size();
                clear_pipeline_output();
                while (!local_queue.empty())
                {
                    local_queue.pop();
                }
                pc = A2B_pipe_port->flush_target();
                THREAD_TRACE("A生产线程", "处理flush",
                             "target=0x" << std::hex << flush_target << std::dec
                                          << " 清空前深度=" << old_depth
                                          << " pc=0x" << std::hex << pc << std::dec);
                continue;
            }

            // 时钟上升沿操作
            if(A2B_pipe_port->fire()) {
                THREAD_TRACE("A生产线程", "握手发送",
                             "payload=0x" << std::hex << local_queue.front() << std::dec
                                           << " 队列深度=" << local_queue.size());
                local_queue.pop();
                A2B_pipe_port->set_valid(false);
            }
            if(local_queue.empty()) {
                process_new_data();
            }
            // 看看是否产生有效新数据了
            if(!local_queue.empty()) {
                A2B_pipe_port->set_valid(true);
                A2B_pipe_port->write(local_queue.front());
                THREAD_TRACE("A生产线程", "驱动输出",
                             "valid=1 payload=0x" << std::hex << local_queue.front() << std::dec
                                                  << " ready=" << A2B_pipe_port->ready()
                                                  << " fire=" << A2B_pipe_port->fire()
                                                  << " 队列深度=" << local_queue.size());
            } else {
                THREAD_TRACE("A生产线程", "无有效输出",
                             "ready=" << A2B_pipe_port->ready()
                                      << " delay=" << delay);
            }
        }

    }

};

class B : public sc_core::sc_module {
public:
    uint32_t pc = 0;
    int delay = 0;
    std::queue<uint32_t> local_queue;
    uint64_t last_flush_epoch = 0;
    sc_core::sc_time clk = sc_core::sc_time(10, sc_core::SC_NS);
    bool busy = 0;
    SC_HAS_PROCESS(B);

    sc_core::sc_port<e203sim::pipeline_consumer_if<uint32_t, uint32_t>> A2B_pipe_port;
    sc_core::sc_port<e203sim::pipeline_producer_if<uint32_t, uint32_t>> B2C_pipe_port;

    B(sc_core::sc_module_name module_name): sc_core::sc_module(module_name) {
        SC_THREAD(stage_thread);
        std::cout << module_name << " created !" << std::endl;
    }
    void clear_pipeline_output()
    {
        // 清输出时同时写一个空 packet，避免后级误读旧数据。
        B2C_pipe_port->set_valid(false);
        B2C_pipe_port->write(0);
        THREAD_TRACE("B生产线程", "清空输出", "valid=0 payload=0");
    }

    void process_receive_data()
    {
        if (delay == 0)
        {
            delay = rand()%3;
            busy = false;
            pc += 8;
            local_queue.push(pc);
            THREAD_TRACE("B消费线程", "处理完成",
                         "pc=0x" << std::hex << pc << std::dec
                                  << " push_to_B2C=0x" << std::hex << pc << std::dec
                                  << " 队列深度=" << local_queue.size()
                                  << " next_delay=" << delay);
        } else {
            THREAD_TRACE("B消费线程", "处理中",
                         "pc=0x" << std::hex << pc << std::dec
                                  << " delay_before=" << delay);
            delay--;
        }

    }

    void stage_thread(){
        A2B_pipe_port->set_ready(true);
        THREAD_TRACE("B流水线程", "初始化", "A2B ready=1");
        clear_pipeline_output();
        sc_core::wait(sc_core::SC_ZERO_TIME);
        while (true)
        {
            sc_core::wait(clk, B2C_pipe_port->flush_event());
            THREAD_TRACE("B流水线程", "线程唤醒",
                         "A2B valid=" << A2B_pipe_port->valid()
                                     << " A2B fire=" << A2B_pipe_port->fire()
                                     << " B2C ready=" << B2C_pipe_port->ready()
                                     << " B2C fire=" << B2C_pipe_port->fire()
                                      << " flush_epoch=" << B2C_pipe_port->flush_epoch()
                                     << " busy=" << busy
                                     << " pc=0x" << std::hex << pc << std::dec
                                     << " delay=" << delay
                                     << " 队列深度=" << local_queue.size());

            if(B2C_pipe_port->flush_epoch() != last_flush_epoch) {
                last_flush_epoch = B2C_pipe_port->flush_epoch();
                const uint32_t flush_target = B2C_pipe_port->flush_target();
                const size_t old_depth = local_queue.size();
                clear_pipeline_output();
                while (!local_queue.empty())
                {
                    local_queue.pop();
                }
                A2B_pipe_port->pulse_flush(B2C_pipe_port->flush_target());
                THREAD_TRACE("B流水线程", "处理flush",
                             "target=0x" << std::hex << flush_target << std::dec
                                          << " 清空前深度=" << old_depth);
                continue;
            }

            // 输出侧上升沿操作
            if(B2C_pipe_port->fire()) {
                THREAD_TRACE("B生产线程", "握手发送",
                             "payload=0x" << std::hex << local_queue.front() << std::dec
                                           << " 队列深度=" << local_queue.size());
                local_queue.pop();
                B2C_pipe_port->set_valid(false);
            }

            // 输入侧上升沿操作
            if(A2B_pipe_port->fire())
            {
                busy = true;
                pc = A2B_pipe_port->read();
                A2B_pipe_port->set_ready(false);
                THREAD_TRACE("B消费线程", "接收数据",
                             "pc=0x" << std::hex << pc << std::dec
                                      << " ready=0");
            }
            if(busy)
            {
                process_receive_data();
            }
            // 看看是否处理完毕
            if(!busy)
            {
                A2B_pipe_port->set_ready(true);
                THREAD_TRACE("B消费线程", "驱动ready", "ready=1");
            } else {
                THREAD_TRACE("B消费线程", "保持busy",
                             "pc=0x" << std::hex << pc << std::dec
                                      << " delay=" << delay
                                     << " fire=" << A2B_pipe_port->fire());
            }

            // 看看是否产生有效新数据了
            if(!local_queue.empty()) {
                B2C_pipe_port->set_valid(true);
                B2C_pipe_port->write(local_queue.front());
                THREAD_TRACE("B生产线程", "驱动输出",
                             "valid=1 payload=0x" << std::hex << local_queue.front() << std::dec
                                                  << " ready=" << B2C_pipe_port->ready()
                                                  << " fire=" << B2C_pipe_port->fire()
                                                  << " 队列深度=" << local_queue.size());
            } else {
                THREAD_TRACE("B生产线程", "无有效输出",
                             "ready=" << B2C_pipe_port->ready());
            }
        }

    }

};

class C : public sc_core::sc_module {
public:
    uint32_t pc = 0;
    int delay = 0;
    sc_core::sc_time clk = sc_core::sc_time(10, sc_core::SC_NS);
    bool busy = 0;
    SC_HAS_PROCESS(C);

    sc_core::sc_port<e203sim::pipeline_consumer_if<uint32_t, uint32_t>> B2C_pipe_port;

    C(sc_core::sc_module_name module_name): sc_core::sc_module(module_name) {
        SC_THREAD(consumer_thread);
        std::cout << module_name << " created !" << std::endl;
    }

    void process_receive_data()
    {
        if (delay == 0)
        {
            delay = rand()%3;
            busy = false;
            THREAD_TRACE("C消费线程", "处理完成",
                         "pc=0x" << std::hex << pc << std::dec
                                  << " next_delay=" << delay);
            if(pc % 8 == 0)
            {
                // 触发flush
                B2C_pipe_port->pulse_flush(pc+10);
                THREAD_TRACE("C消费线程", "发出flush",
                             "target=0x" << std::hex << (pc + 10) << std::dec);
            }
        } else {
            THREAD_TRACE("C消费线程", "处理中",
                         "pc=0x" << std::hex << pc << std::dec
                                  << " delay_before=" << delay);
            delay--;
        }

    }
    void consumer_thread(){
        B2C_pipe_port->set_ready(true);
        THREAD_TRACE("C消费线程", "初始化", "ready=1");
        sc_core::wait(sc_core::SC_ZERO_TIME);
        while (true)
        {
            sc_core::wait(clk);
            THREAD_TRACE("C消费线程", "线程唤醒",
                         "valid=" << B2C_pipe_port->valid()
                                  << " fire=" << B2C_pipe_port->fire()
                                  << " busy=" << busy
                                  << " pc=0x" << std::hex << pc << std::dec
                                  << " delay=" << delay);
            // 上升沿操作
            if(B2C_pipe_port->fire())
            {
                busy = true;
                pc = B2C_pipe_port->read();
                B2C_pipe_port->set_ready(false);
                THREAD_TRACE("C消费线程", "接收数据",
                             "pc=0x" << std::hex << pc << std::dec
                                      << " ready=0");
            }
            if(busy)
            {
                process_receive_data();
            }
            // 看看是否处理完毕
            if(!busy)
            {
                B2C_pipe_port->set_ready(true);
                THREAD_TRACE("C消费线程", "驱动ready", "ready=1");
            } else {
                THREAD_TRACE("C消费线程", "保持busy",
                             "pc=0x" << std::hex << pc << std::dec
                                      << " delay=" << delay
                                      << " fire=" << B2C_pipe_port->fire());
            }
        }

    }

};

class Top : public sc_core::sc_module {
public:
    SC_HAS_PROCESS(Top);

    e203sim::pipeline_channel<uint32_t, uint32_t> A2B_pipe;
    e203sim::pipeline_channel<uint32_t, uint32_t> B2C_pipe;
    A producer;
    B middle;
    C consumer;

    explicit Top(sc_core::sc_module_name module_name)
        : sc_core::sc_module(module_name),
          A2B_pipe("A2B_pipe"),
          B2C_pipe("B2C_pipe"),
          producer("producer"),
          middle("middle"),
          consumer("consumer")
    {
        producer.A2B_pipe_port.bind(A2B_pipe);
        middle.A2B_pipe_port.bind(A2B_pipe);
        middle.B2C_pipe_port.bind(B2C_pipe);
        consumer.B2C_pipe_port.bind(B2C_pipe);

        SC_THREAD(monitor_thread);
    }

private:
    void monitor_thread()
    {
        const sc_core::sc_time clk(10, sc_core::SC_NS);
        while (true) {
            sc_core::wait(clk);
            THREAD_TRACE("Top监视线程", A2B_pipe.fire() ? "A2B fire" : "A2B 采样",
                         "valid=" << A2B_pipe.valid()
                                  << " ready=" << A2B_pipe.ready()
                                  << " payload=0x" << std::hex << A2B_pipe.read() << std::dec
                                  << " flush_epoch=" << A2B_pipe.flush_epoch()
                                  << " flush_target=0x" << std::hex << A2B_pipe.flush_target()
                                  << std::dec);
            THREAD_TRACE("Top监视线程", B2C_pipe.fire() ? "B2C fire" : "B2C 采样",
                         "valid=" << B2C_pipe.valid()
                                  << " ready=" << B2C_pipe.ready()
                                  << " payload=0x" << std::hex << B2C_pipe.read() << std::dec
                                  << " flush_epoch=" << B2C_pipe.flush_epoch()
                                  << " flush_target=0x" << std::hex << B2C_pipe.flush_target()
                                  << std::dec);
        }
    }
};

int sc_main(int argc, char* argv[])
{
    const char* trace_path = argc > 1 ? argv[1] : "tmp/pipeline_channel_thread_trace.log";
    const sc_core::sc_time sim_time = argc > 2
        ? sc_core::sc_time(static_cast<double>(std::strtoull(argv[2], nullptr, 0)), sc_core::SC_NS)
        : sc_core::sc_time(300, sc_core::SC_NS);

    std::srand(1);
    e203sim::thread_trace::instance().enable(trace_path, 10);

    Top top("top");
    sc_core::sc_start(sim_time);

    e203sim::thread_trace::instance().disable();
    std::cout << "pipeline channel test finished at "
              << sc_core::sc_time_stamp()
              << ", trace=" << trace_path << std::endl;
    return 0;
}
