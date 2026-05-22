#include "common/pipeline_if.h"
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
    }
    void process_new_data()
    {
        if (delay == 0)
        {
            pc += 4;
            local_queue.push(pc);
            delay = rand()%3;
        } else {
            delay--;
        }

    }
    void produce_thread(){
        clear_pipeline_output();
        sc_core::wait(sc_core::SC_ZERO_TIME);
        while (true)
        {
            sc_core::wait(clk, A2B_pipe_port->flush_event());

            if(A2B_pipe_port->flush_epoch() != last_flush_epoch) {
                last_flush_epoch = A2B_pipe_port->flush_epoch();
                clear_pipeline_output();
                while (!local_queue.empty())
                {
                    local_queue.pop();
                }
                pc = A2B_pipe_port->flush_target();
                continue;
            }

            // 时钟上升沿操作
            if(A2B_pipe_port->fire()) {
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
    }

    void process_receive_data()
    {
        if (delay == 0)
        {
            delay = rand()%3;
            busy = false;
            pc += 8;
            local_queue.push(pc);
        } else {
            delay--;
        }

    }

    void stage_thread(){
        A2B_pipe_port->set_ready(true);
        clear_pipeline_output();
        sc_core::wait(sc_core::SC_ZERO_TIME);
        while (true)
        {
            sc_core::wait(clk, B2C_pipe_port->flush_event());

            if(B2C_pipe_port->flush_epoch() != last_flush_epoch) {
                last_flush_epoch = B2C_pipe_port->flush_epoch();
                clear_pipeline_output();
                while (!local_queue.empty())
                {
                    local_queue.pop();
                }
                A2B_pipe_port->pulse_flush(B2C_pipe_port->flush_target());
                continue;
            }

            // 输出侧上升沿操作
            if(B2C_pipe_port->fire()) {
                local_queue.pop();
                B2C_pipe_port->set_valid(false);
            }

            // 输入侧上升沿操作
            if(A2B_pipe_port->fire())
            {
                busy = true;
                pc = A2B_pipe_port->read();
                A2B_pipe_port->set_ready(false);
            }
            if(busy)
            {
                process_receive_data();
            }
            // 看看是否处理完毕
            if(!busy)
            {
                A2B_pipe_port->set_ready(true);
            }

            // 看看是否产生有效新数据了
            if(!local_queue.empty()) {
                B2C_pipe_port->set_valid(true);
                B2C_pipe_port->write(local_queue.front());
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
            if(pc % 8 == 0)
            {
                // 触发flush
                B2C_pipe_port->pulse_flush(pc+10);
            }
        } else {
            delay--;
        }

    }
    void consumer_thread(){
        B2C_pipe_port->set_ready(true);
        sc_core::wait(sc_core::SC_ZERO_TIME);
        while (true)
        {
            sc_core::wait(clk);
            // 上升沿操作
            if(B2C_pipe_port->fire())
            {
                busy = true;
                pc = B2C_pipe_port->read();
                B2C_pipe_port->set_ready(false);
            }
            if(busy)
            {
                process_receive_data();
            }
            // 看看是否处理完毕
            if(!busy)
            {
                B2C_pipe_port->set_ready(true);
            }
        }

    }

};
