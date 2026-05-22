#pragma once

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace e203sim {

class thread_trace
{
public:
    static thread_trace& instance();

    void enable(const std::string& path, uint32_t cycle_ns);
    void disable();
    bool enabled() const;

    uint64_t now_tick() const;
    uint64_t cycle_tick() const;

    void emit(const std::string& thread, const std::string& event, const std::string& detail);

private:
    thread_trace() = default;

    std::ofstream file_;
    bool enabled_ = false;
    uint64_t cycle_tick_ = 10000;
};

} // namespace e203sim

#define THREAD_TRACE(thread_name, event_name, detail_expr)                 \
    do {                                                                  \
        if (e203sim::thread_trace::instance().enabled()) {                \
            std::ostringstream trace_detail_oss;                          \
            trace_detail_oss << detail_expr;                              \
            e203sim::thread_trace::instance().emit(                       \
                (thread_name), (event_name), trace_detail_oss.str());     \
        }                                                                 \
    } while (0)

