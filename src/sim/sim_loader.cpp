#include "sim/sim_loader.h"

#include "third_party/json.hpp"

#include <getopt.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

uint32_t parse_u32_json(const nlohmann::json& j,
                        const char* key,
                        uint32_t default_value = 0)
{
    if (!j.contains(key) || j.at(key).is_null()) {
        return default_value;
    }

    const auto& value = j.at(key);

    if (value.is_number_unsigned()) {
        return value.get<uint32_t>();
    }

    if (value.is_number_integer()) {
        const auto n = value.get<int64_t>();
        if (n < 0 || n > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error(std::string("invalid uint32 value for ") + key);
        }
        return static_cast<uint32_t>(n);
    }

    if (value.is_string()) {
        const auto s = value.get<std::string>();
        const auto n = std::stoul(s, nullptr, 0);
        if (n > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error(std::string("invalid uint32 value for ") + key);
        }
        return static_cast<uint32_t>(n);
    }

    throw std::runtime_error(std::string("invalid type for ") + key);
}

void print_usage(const char* program)
{
    std::cout << "Usage: " << program << " [OPTION...] [args]" << std::endl;
    std::cout << std::left << std::setw(24) << "-h,--help"
              << "打印帮助信息" << std::endl;
    std::cout << std::left << std::setw(24) << "-c,--config=FILE"
              << "配置文件导入" << std::endl;
    std::cout << std::left << std::setw(24) << "-i,--image=FILE"
              << "执行bin文件导入" << std::endl;
}

} // namespace

namespace e203sim {

int sim_loader::parse_args(int argc, char* argv[], args_option& option) const
{
    const struct option table[] = {
        {"config", required_argument, nullptr, 'c'},
        {"image", required_argument, nullptr, 'i'},
        {"help", no_argument, nullptr, 'h'},
        {0, 0, nullptr, 0},
    };

    int o;
    while ((o = getopt_long(argc, argv, "-c:i:h", table, nullptr)) != -1) {
        switch (o) {
        case 'c':
            option.config_path = optarg;
            break;
        case 'i':
            option.bin_path = optarg;
            break;
        default:
            print_usage(argv[0]);
            exit(0);
        }
    }

    return 0;
}

int sim_loader::load_config(const std::string& config_path, sim_config& cfg) const
{
    std::ifstream file(config_path);
    if (!file.is_open()) {
        throw std::runtime_error("cannot open config file: " + config_path);
    }

    nlohmann::json j;
    file >> j;
    cfg.cycle_ns = j.value("cycle_ns", 10u);
    cfg.memory_config_path = j.value("memory_config_path", "");
    cfg.enable_debug = j.value("enable_debug", false);
    cfg.debug_path = j.value("debug_path", "debug.txt");
    cfg.print();

    std::ifstream mem_file(cfg.memory_config_path);
    if (!mem_file.is_open()) {
        throw std::runtime_error("cannot open memory config file: " + cfg.memory_config_path);
    }

    nlohmann::json mem_j;
    mem_file >> mem_j;
    auto parse_memory_config = [](const nlohmann::json& mem_json, memory_config* mem_cfg) {
        mem_cfg->name = mem_json.value("name", "");
        mem_cfg->base_addr = parse_u32_json(mem_json, "base", 0);
        mem_cfg->size = parse_u32_json(mem_json, "size", 0);
        mem_cfg->read_latency_cycles = parse_u32_json(mem_json, "read_latency_cycles", 1);
        mem_cfg->write_latency_cycles = parse_u32_json(mem_json, "write_latency_cycles", 1);
        mem_cfg->print();
    };

    for (const auto& region : mem_j["regions"]) {
        auto name = region.value("name", "");
        if (name == "itcm") {
            parse_memory_config(region, cfg.itcm);
        } else if (name == "dtcm") {
            parse_memory_config(region, cfg.dtcm);
        } else if (name == "clint") {
            parse_memory_config(region, cfg.clint);
        } else if (name == "plic") {
            parse_memory_config(region, cfg.plic);
        } else if (name == "ppi") {
            parse_memory_config(region, cfg.ppi);
        } else if (name == "fio") {
            parse_memory_config(region, cfg.fio);
        }
    }

    return 0;
}

} // namespace e203sim
