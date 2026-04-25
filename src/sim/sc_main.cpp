#include "platform/e203_soc.h"
#include "common/types.h"
#include <getopt.h>
#include <iostream>
#include <iomanip>

using namespace sc_core;
using namespace std;

int parse_args(int argc, char* argv[], e203sim::args_option& option)
{
  const struct option table[] = {
    {"config"     , required_argument, nullptr, 'c'},
    {"image"      , required_argument, nullptr, 'i'},
    {"help"       , no_argument      , nullptr, 'h'},
    {0            , 0                , nullptr,  0 },
  };
  int o;
  while ((o = getopt_long(argc, argv, "-c:i:h", table, nullptr)) != -1)
  {
    switch (o)
    {
    case 'c' : option.config_path = optarg;
        break;
    case 'i' : option.bin_path = optarg;
        break;
    default:
        cout << "Usage: " << argv[0] << " [OPTION...] [args]" << endl;
        cout << left << setw(24) << "-h,--help"
            << "打印帮助信息" << endl;
        cout << left << setw(24) << "-c,--config=FILE"
            << "配置文件导入" << endl;
        cout << left << setw(24) << "-i,--image=FILE"
            << "执行bin文件导入" << endl;
        exit(0);
    }
  }
  return 0;
}

int sc_main(int argc, char* argv[])
{
    // TODO: 解析命令行
    for (int i = 0; i < argc; i++)
    {
        cout << "argv[" << i << "] " << argv[i] << endl;
    }
    e203sim::args_option option;
    parse_args(argc, argv, option);
    // 创建platform
    e203_soc soc("e203_soc");
    sc_start();
    return 0;
}