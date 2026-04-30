#include "platform/e203_soc.h"
#include "common/types.h"
#include "common/debug_logger.h"
#include "sim/sim_loader.h"

using namespace sc_core;
int sc_main(int argc, char* argv[])
{
    e203sim::sim_loader loader;
    e203sim::args_option option;
    loader.parse_args(argc, argv, option);
    // option.print();
    // 解析JSON配置
    e203sim::sim_config cfg;
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
    // 创建platform
    e203_soc soc("e203_soc", cfg);
    sc_start(200, SC_NS);
    return 0;
}
