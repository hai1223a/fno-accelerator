#pragma once

#include <cstdint>
#include <systemc>

namespace e203sim {

// 通用流水线生产者接口。
// PayloadT 是前向传递的数据类型，FlushT 是后向 flush 携带的数据类型。
//
// 前向数据通道采用 valid/ready 握手：producer 写 payload 并拉高 valid，
// consumer 拉高 ready 表示本周期接收。
//
// flush 是后级指向前级的高优先级控制事件，不参与 valid/ready 握手。
// consumer 在 branch/trap/fence 等场景递增 flush epoch 并携带 target；producer 保存
// 自己已经处理过的 epoch，只在观察到新 epoch 时清空本地队列并重定向取指。
// 这样避免 SystemC delta 调度下 pulse 清早/清晚导致的漏处理或重复处理。
template <typename PayloadT, typename FlushT>
class pipeline_producer_if : virtual public sc_core::sc_interface
{
public:
    virtual void set_valid(bool valid) = 0;
    virtual void write(const PayloadT& payload) = 0;
    virtual bool fire() const = 0;
    virtual bool ready() const = 0;
    virtual uint64_t flush_epoch() const = 0;
    virtual FlushT flush_target() const = 0;
    virtual const sc_core::sc_event& flush_event() const = 0;
};

// 通用流水线消费者接口。
// 消费者观察 valid/payload，驱动 ready。
// 对 IFU->EXU 来说，EXU 是 consumer：它用 ready 控制 IFU 是否弹出队首 packet，
// 并在 branch/trap/fence 等重定向场景用 pulse_flush() 递增 flush epoch 回冲 IFU。
template <typename PayloadT, typename FlushT>
class pipeline_consumer_if : virtual public sc_core::sc_interface
{
public:
    virtual bool valid() const = 0;
    virtual bool fire() const = 0;
    virtual PayloadT read() const = 0;
    virtual void set_ready(bool ready) = 0;
    virtual void pulse_flush(const FlushT& target) = 0;
    virtual uint64_t flush_epoch() const = 0;
};

// 可例化的 SystemC 通道，用于连接任意两级内部流水线。
// 这里用 sc_signal 保存状态，但使用者只依赖接口模板，不直接依赖具体信号成员。
//
// 注意 SystemC 的 sc_signal 写入要到 delta cycle 后才稳定。因此需要建模“同周期组合路径”
// 时，模块线程通常会显式 wait(SC_ZERO_TIME)，让对端刚写入的 valid/ready/flush 信号可见。
// 推荐 producer 线程按“先处理上一拍 fire，再生成/驱动下一拍输出”的顺序推进；同时承担
// consumer+producer 职责的中间级应合并到一个线程内，避免两个线程同拍调度顺序影响吞吐。
// 本通道只提供信号和事件，不隐式推进时间，时序关系由 IFU/EXU 线程显式建模。
template <typename PayloadT, typename FlushT>
class pipeline_channel : public sc_core::sc_channel,
                         public pipeline_producer_if<PayloadT, FlushT>,
                         public pipeline_consumer_if<PayloadT, FlushT>
{
public:
    explicit pipeline_channel(sc_core::sc_module_name name)
        : sc_core::sc_channel(name),
          valid_("valid"),
          ready_("ready"),
          payload_("payload"),
          flush_epoch_(0),
          flush_target_()
    {
    }

    void set_valid(bool valid) override
    {
        valid_.write(valid);
    }

    void write(const PayloadT& payload) override
    {
        payload_.write(payload);
    }

    bool ready() const override
    {
        return ready_.read();
    }

    uint64_t flush_epoch() const override
    {
        return flush_epoch_;
    }

    FlushT flush_target() const override
    {
        return flush_target_;
    }

    const sc_core::sc_event& flush_event() const override
    {
        return flush_event_;
    }

    bool valid() const override
    {
        return valid_.read();
    }

    PayloadT read() const override
    {
        return payload_.read();
    }

    bool fire() const override
    {
        return valid_.read() && ready_.read();
    }

    void set_ready(bool ready) override
    {
        ready_.write(ready);
    }

    void pulse_flush(const FlushT& target) override
    {
        flush_target_ = target;
        ++flush_epoch_;
        // flush 是后向控制事件，后级产生后需要让前级在同一仿真时间被唤醒。
        // 用 SC_ZERO_TIME 事件模拟 RTL 中 pipe_flush_req -> IFU pc_nxt 的组合可见性；
        // epoch 由接收方自行去重，不需要清除。
        flush_event_.notify(sc_core::SC_ZERO_TIME);
    }

private:
    sc_core::sc_signal<bool> valid_;
    sc_core::sc_signal<bool> ready_;
    sc_core::sc_signal<PayloadT> payload_;
    uint64_t flush_epoch_;
    FlushT flush_target_;
    sc_core::sc_event flush_event_;
};

} // namespace e203sim
