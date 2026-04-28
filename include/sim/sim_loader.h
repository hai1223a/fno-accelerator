#pragma once

#include "common/types.h"

#include <string>

namespace e203sim {

class sim_loader
{
public:
    int parse_args(int argc, char* argv[], args_option& option) const;
    int load_config(const std::string& config_path, sim_config& cfg) const;
};

} // namespace e203sim
