# 工程架构图
## 目录结构

```text
.
├── configs/              # 仿真配置文件
├── docs/                 # 项目文档和 E203 参考资料
├── include/              # SystemC/C++ 模型头文件
├── src/                  # SystemC/C++ 模型实现
├── programs/             # 后续用于仿真的裸机程序、链接脚本和构建脚本
├── scripts/              # 项目辅助脚本
├── tests/                # 单元测试、模块测试和集成测试
├── third_party/          # 第三方参考项目和 RTL 源码
├── build/                # 编译输出目录，由 make 生成
├── Makefile              # 项目构建入口
└── README.md             # 项目说明文档

```

### configs/

存放仿真运行配置。

- e203sim.json：仿真主配置，例如时钟周期、调试日志开关、内存配置路径等。
- memory.json：SoC 地址空间配置，例如 ITCM、DTCM、CLINT、PLIC、PPI、FIO 等模块的 base address、size 和访问延迟。

### docs/

存放项目说明和参考资料。

- hbirdv2_soc.pdf：E203/HBirdv2 SoC 技术文档，是建模时对照 RTL 语义的重要参考。
- project.md：项目设计、阶段性说明或建模笔记。

### include/

存放模型头文件，目录结构基本与 src/ 对应。

- include/common/：通用类型、日志宏、基础工具定义。
- include/sim/：仿真配置加载、启动辅助接口。
- include/platform/：SoC 顶层平台模型声明。
- include/models/core/：core 相关模型声明
- include/models/memory/：存储模型声明
- include/models/bus/：总线/路由模型声明
- include/models/peripherals/：外设模型声明
- include/models/nice/：NICE 协处理器/加速器接口相关模型声明
- include/isa/：ISA 相关建模声明

### src/

存放模型实现代码，和 include/ 中的头文件一一对应。

- src/sim/：仿真入口、配置加载、debug logger 实现。
- src/platform/：e203_soc 顶层平台连接实现。
- src/models/core/：CPU/core/LSU/IFU 的 SystemC/TLM 风格模型。
- src/models/memory/：ITCM、DTCM、SRAM 等存储模型实现。
- src/models/bus/：地址路由和总线转发模型。
- src/models/peripherals/：CLINT、PLIC、PPI、FIO、mem 等外设模型。
- src/models/nice/：NICE 加速器模型。
- src/common/src/isa/：预留给通用实现和 ISA 行为模型。

### programs/

预留给后续运行在模型上的软件程序。

- programs/baremetal/：裸机测试程序。
- programs/linker/：链接脚本。
- programs/scripts/：程序编译、转换、加载相关脚本。

### tests/

预留给测试体系。

- tests/unit/：测试纯 C++ 逻辑
- tests/component/：单独实例化一个 SystemC/TLM 模块，并用 testbench initiator 发 transaction。
- tests/integration/：测试多个模块连接后的行为

### third_party/

存放第三方参考工程和原始 RTL。

- third_party/e203_hbirdv2/：E203/HBirdv2 官方源码、RTL、文档、仿真工程，是本项目 SystemC 建模的主要语义参考。
- third_party/RISC-V-TLM/：RISC-V TLM 参考项目，可用于借鉴 TLM 建模方式。

### build/

编译输出目录，由 make build 自动生成。包含 .o 文件、依赖文件和最终仿真可执行文件 build/e203-sim。该目录不应作为源码维护。