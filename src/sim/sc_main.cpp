#include "platform/e203_soc.h"

using namespace sc_core;
using namespace std;
int sc_main(int argc, char* argv[]) 
{
    // TODO: 解析命令行
    for (int i = 0; i < argc; i++)
    {
        cout << "argv[" << i << "] " << argv[i] << endl;
    }
    ;
    // 创建platform
    e203_soc soc("e203_soc");
    sc_start();
    return 0;
}