#include "common/debug_logger.h"

#include <stdexcept>

namespace e203sim {

debug_logger& debug_logger::instance()
{
    static debug_logger logger;
    return logger;
}

void debug_logger::enable(const std::string& path)
{
    file_.open(path, std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        throw std::runtime_error("cannot open debug file: " + path);
    }

    enabled_ = true;
}

void debug_logger::disable()
{
    enabled_ = false;

    if (file_.is_open()) {
        file_.close();
    }
}

bool debug_logger::enabled() const
{
    return enabled_;
}

void debug_logger::log(const std::string& msg)
{
    if (!enabled_ || !file_.is_open()) {
        return;
    }

    file_ << msg << '\n';
}

} // namespace e203sim
