#include "platform/e203_soc.h"
#include "common/types.h"
#include "common/debug_logger.h"
#include "sim/difftest_manager.h"
#include "sim/pipe_trace.h"
#include "sim/sim_loader.h"
#include "sim/thread_trace.h"

#include <fstream>
#include <iterator>
#include <vector>

using namespace sc_core;

namespace {

std::vector<uint8_t> read_binary_image(const std::string& path)
{
    if (path.empty()) {
        return {};
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SIM_ERROR("sc_main", "cannot open binary image for difftest: " << path);
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
}

} // namespace

int sc_main(int argc, char* argv[])
{
    e203sim::sim_loader loader;
    e203sim::args_option option;
    loader.parse_args(argc, argv, option);
    // option.print();
    // 解析JSON配置
    e203sim::sim_config cfg;
    cfg.bin_path = option.bin_path;
    cfg.itcm = new e203sim::memory_config;
    cfg.dtcm = new e203sim::memory_config;
    cfg.clint = new e203sim::memory_config;
    cfg.plic = new e203sim::memory_config;
    cfg.ppi = new e203sim::memory_config;
    cfg.fio = new e203sim::memory_config;
    loader.load_config(option.config_path, cfg);
    if (cfg.enable_debug) {
        e203sim::debug_logger::instance().enable(cfg.debug_path);
    }
    if (cfg.enable_pipe_trace) {
        e203sim::pipe_trace::instance().enable(cfg.pipe_trace_path, cfg.cycle_ns);
    }
    if (cfg.enable_thread_trace) {
        e203sim::thread_trace::instance().enable(cfg.thread_trace_path, cfg.cycle_ns);
    }
    // 创建platform
    e203_soc soc("e203_soc", cfg);
    soc.load_itcm_binary(cfg.bin_path, cfg.load_addr);
    if (cfg.enable_difftest) {
        e203sim::difftest_manager::instance().init(cfg, read_binary_image(cfg.bin_path));
    }
    sc_start();
    return 0;
}
