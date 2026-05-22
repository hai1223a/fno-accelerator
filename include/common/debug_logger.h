#pragma once

#include <fstream>
#include <sstream>
#include <string>

namespace e203sim {

class debug_logger
{
public:
    static debug_logger& instance();

    void enable(const std::string& path);
    void disable();
    bool enabled() const;
    void log(const std::string& msg);

private:
    debug_logger() = default;

    bool enabled_ = false;
    std::ofstream file_;
};

} // namespace e203sim

#define INFO(expr)                                \
    do {                                                       \
        if (e203sim::debug_logger::instance().enabled()) {     \
            std::ostringstream oss;                            \
            oss << expr;                                       \
            e203sim::debug_logger::instance().log(oss.str());  \
        }                                                      \
    } while (0)

#define SIM_ERROR(module, msg)                          \
      do {                                              \
        std::ostringstream oss;                         \
        oss << "[ERROR] " << sc_core::sc_time_stamp()   \
            << " [" << module << "] " << msg;           \
        INFO(oss.str());                   \
        std::cerr << oss.str() << std::endl;            \
        exit(1); \
      } while (0)

#define SIM_INFO(module, msg)                          \
      do {                                              \
        std::ostringstream oss;                         \
        oss << "[INFO] " << sc_core::sc_time_stamp()   \
            << " [" << module << "] " << msg;           \
        INFO(oss.str());                   \
        if (e203sim::debug_logger::instance().enabled()) { \
            std::cout << oss.str() << std::endl;            \
        }\
      } while (0)

// 架构异常日志：被模拟 CPU 可见的 trap，例如 illegal instruction、ecall、
// access fault。该宏只记录，不终止仿真；commit 侧仍负责写 CSR 和 flush。
#define SIM_ARCH_EXCEPTION(module, msg)                 \
      do {                                              \
        std::ostringstream oss;                         \
        oss << "[ARCH_EXCEPTION] " << sc_core::sc_time_stamp() \
            << " [" << module << "] " << msg;           \
        INFO(oss.str());                                \
        if (e203sim::debug_logger::instance().enabled()) { \
            std::cout << oss.str() << std::endl;        \
        }                                               \
      } while (0)

#define SIM_ASSERT(cond, msg)                          \
    do {                                                           \
        if (!(cond)) {                                             \
            std::cerr << "[ASSERT] " << sc_core::sc_time_stamp()   \
            << " " << __FILE__ << ":" << __LINE__              \
            << " " << __func__ << "(): " << msg << std::endl;  \
            std::abort();                                          \
        }                                                           \
        } while (0)
