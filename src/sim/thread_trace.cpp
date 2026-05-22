#include "sim/thread_trace.h"

#include "common/debug_logger.h"

#include <cmath>
#include <systemc>

namespace {

std::string sanitize(std::string text)
{
    for (char& ch : text) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    return text;
}

} // namespace

namespace e203sim {

thread_trace& thread_trace::instance()
{
    static thread_trace trace;
    return trace;
}

void thread_trace::enable(const std::string& path, uint32_t cycle_ns)
{
    file_.close();
    file_.open(path);
    if (!file_.is_open()) {
        SIM_ERROR("thread_trace", "cannot open thread trace file: " << path);
    }
    enabled_ = true;
    cycle_tick_ = static_cast<uint64_t>(cycle_ns == 0 ? 1 : cycle_ns) * 1000ull;
    file_ << "# 时间ps 周期 线程 事件 说明\n";
}

void thread_trace::disable()
{
    enabled_ = false;
    file_.close();
}

bool thread_trace::enabled() const
{
    return enabled_;
}

uint64_t thread_trace::now_tick() const
{
    return static_cast<uint64_t>(std::llround(sc_core::sc_time_stamp().to_seconds() * 1000000000000.0));
}

uint64_t thread_trace::cycle_tick() const
{
    return cycle_tick_;
}

void thread_trace::emit(const std::string& thread, const std::string& event, const std::string& detail)
{
    if (!enabled_) {
        return;
    }
    const uint64_t tick = now_tick();
    const uint64_t cycle = cycle_tick_ == 0 ? 0 : tick / cycle_tick_;
    file_ << tick << ' '
          << cycle << ' '
          << sanitize(thread) << ' '
          << sanitize(event) << ' '
          << sanitize(detail) << '\n';
}

} // namespace e203sim
