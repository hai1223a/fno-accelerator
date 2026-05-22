#include "sim/pipe_trace.h"

#include "common/debug_logger.h"

#include <iomanip>
#include <cmath>
#include <sstream>
#include <systemc>

namespace {

std::string hex32(uint32_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return oss.str();
}

std::string sanitize_o3_text(std::string text)
{
    for (char& ch : text) {
        if (ch == ':' || ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    return text;
}

} // namespace

namespace e203sim {

pipe_trace& pipe_trace::instance()
{
    static pipe_trace trace;
    return trace;
}

void pipe_trace::enable(const std::string& path, uint32_t cycle_ns)
{
    file_.close();
    file_.open(path);
    if (!file_.is_open()) {
        SIM_ERROR("pipe_trace", "cannot open pipe trace file: " << path);
    }
    enabled_ = true;
    next_seq_ = 0;
    cycle_tick_ = static_cast<uint64_t>(cycle_ns == 0 ? 1 : cycle_ns) * 1000ull;
}

void pipe_trace::disable()
{
    enabled_ = false;
    file_.close();
}

bool pipe_trace::enabled() const
{
    return enabled_;
}

uint64_t pipe_trace::next_seq()
{
    return next_seq_++;
}

uint64_t pipe_trace::now_tick() const
{
    return static_cast<uint64_t>(std::llround(sc_core::sc_time_stamp().to_seconds() * 1000000000000.0));
}

uint64_t pipe_trace::cycle_tick() const
{
    return cycle_tick_;
}

void pipe_trace::emit_instruction(uint64_t seq,
                                  addr_t pc,
                                  uint32_t inst,
                                  uint64_t fetch_tick,
                                  uint64_t decode_tick,
                                  uint64_t dispatch_tick,
                                  uint64_t issue_tick,
                                  uint64_t complete_tick,
                                  uint64_t retire_tick,
                                  const std::string& disasm)
{
    if (!enabled_) {
        return;
    }

    const std::string label = sanitize_o3_text(disasm.empty() ? hex32(inst) : disasm);

    // Keep the model's real CA timing.  Same-cycle stages intentionally share
    // the same tick; only stalls or long-pipe waits should make a stage longer.
    file_ << "O3PipeView:fetch:" << fetch_tick << ":" << hex32(pc) << ":0:" << seq << ": " << label << '\n'
          << "O3PipeView:decode:" << decode_tick << '\n'
          << "O3PipeView:rename:" << decode_tick << '\n'
          << "O3PipeView:dispatch:" << dispatch_tick << '\n'
          << "O3PipeView:issue:" << issue_tick << '\n'
          << "O3PipeView:complete:" << complete_tick << '\n'
          << "O3PipeView:retire:" << retire_tick << '\n';
}

void pipe_trace::emit_detail(uint64_t tick,
                             const std::string& source,
                             uint64_t seq,
                             const std::string& message)
{
    if (!enabled_) {
        return;
    }
    file_ << tick << ": " << sanitize_o3_text(source)
          << ": " << sanitize_o3_text(message)
          << " [sn:" << seq << "]\n";
}

} // namespace e203sim
