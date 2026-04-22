# 工程架构图

## 文件夹粒度

```text
e203-systemc/
├── docs/          # 设计文档、建模决策、从 hbirdv2_soc.pdf 提炼出的结构说明
├── configs/       # 仿真配置：模型精度、memory map、延迟参数、启用模块
├── include/       # 对外稳定头文件：接口、公共类型、模型声明
├── src/           # SystemC/TLM 模型实现主体
├── tests/         # 单元测试、集成测试、trace/golden 对比
├── programs/      # 裸机测试程序、linker script、程序构建辅助文件
├── scripts/       # 运行仿真、构建测试程序、trace 处理等脚本
└── third_party/   # 第三方依赖说明或 vendored 依赖
```

## 核心代码分层

```text
src/
├── common/        # 配置、日志、trace、memory map、通用类型
├── isa/           # RISC-V 指令语义、decoder、CSR、trap，共享给 AT/CA
├── platform/      # SoC 组装层，只负责连接 Core/Bus/Memory/NICE/外设
├── models/        # 可替换模型实现：Core、Bus、Memory、NICE、外设
└── sim/           # 仿真入口、命令行参数、ELF 加载、sc_main
```

## 可替换模型层

```text
src/models/
├── core/          # E203 Core；先实现 CoreAT，后续增加 CoreCA
├── bus/           # TLM router/interconnect；先 b_transport，后续可加 CA 总线
├── memory/        # ITCM/DTCM/Flash/RAM；先数组模型，后续加周期延迟
├── nice/          # NICE 协处理器接口；NullNice、NiceAT、NiceCA
└── peripherals/   # CLINT/PLIC/UARTLite/StubPeripheral
```

## 带注释模块图

```text
                     +----------------------+
                     |      E203Platform    |
                     |  SoC 组装，不写行为逻辑 |
                     +----------+-----------+
                                |
        +-----------------------+-----------------------+
        |                       |                       |
        v                       v                       v
+---------------+       +---------------+       +----------------+
|   Core Model  |       |  Bus / Router |       | Peripherals    |
| CoreAT/CoreCA |<----->| TLM transport |<----->| CLINT/PLIC/UART|
+-------+-------+       +-------+-------+       | Stub devices   |
        |                       |               +----------------+
        |                       |
        v                       v
+---------------+       +---------------+
|  NICE CoUnit  |       | Memory System |
| Null/AT/CA    |       | ITCM/DTCM/RAM |
+---------------+       +---------------+
```

注释：

- `E203Platform` 只负责把模块连接起来，不关心模块内部是 AT 还是 CA。
- `Core Model` 是 CPU 建模的主入口；第一版用 `CoreAT` 跑通功能，后续在内部演进 `CoreCA`。
- `Bus / Router` 是主要 TLM 边界；第一版使用 `b_transport` 做地址解码和转发。
- `Memory System` 覆盖 ITCM、DTCM、Flash、RAM 等存储空间。
- `NICE CoUnit` 独立成插件边界，便于替换 `NullNice`、`NiceAT`、`NiceCA`。
- `Peripherals` 只认真实现 CLINT、PLIC、UARTLite；GPIO/I2C/PWM 等先挂 `StubPeripheral`。

## 建模边界原则

```text
跨大模块通信：使用 SystemC/TLM。
Core 内部流水线：CoreCA 内部使用自定义 cycle channel。
AT/CA 共享功能：decoder、CSR、trap、指令语义放在 src/isa/。
外设建模策略：关键外设最小实现，非关键外设先 stub。
```

## 参考文件职责注释

说明：下面是工程逐步长出来时的参考文件职责。如果项目统一使用 `.h`，则把示例中的 `.hpp` 等价替换为 `.h` 即可。

```text
Makefile
  # 第一阶段使用 Makefile 管理构建。
  # 目标：make build / make run / make test / make clean。

configs/
├── e203sim.json
│   # 本次仿真选择：core/bus/memory/nice 使用 AT、CA 还是 none/null。
│   # 第一阶段 core=none，用于 TLM 闭环测试；后续改成 core=at。
└── memory.json
    # E203 官方默认 memory map。
    # RouterAT 根据这里的地址区间把访问转发到 ITCM/DTCM/CLINT/PLIC/PPI/FIO。

include/
├── common/
│   ├── types.h
│   │   # 基础类型：addr_t、word_t、AccessSize 等。
│   ├── config.h
│   │   # SimConfig、ModelConfig、MemoryRegionConfig 等配置结构。
│   └── memory_map.h
│       # AddressRegion、MemoryMap、地址 decode 接口。
│
├── interfaces/
│   ├── irq_if.h
│   │   # Core 接收 CLINT/PLIC 中断的稳定接口。第一阶段可先占位。
│   ├── nice_if.h
│   │   # Core <-> NICE 的自定义接口。第一阶段可先占位。
│   ├── tick_if.h
│   │   # CA 模型统一 tick 接口。第一阶段可先占位。
│   └── trace_if.h
│       # trace sink/source 接口。第一阶段可先占位。
│
├── platform/
│   └── e203_platform.h
│       # SoC 组装层声明，只暴露平台构造和顶层连接。
│
└── models/
    ├── bus/router_at.h
    │   # TLM target/router 声明。第一阶段重点实现。
    ├── memory/memory_at.h
    │   # TLM memory target 声明。第一阶段重点实现。
    ├── peripherals/stub_peripheral.h
    │   # 通用外设 stub。第一阶段重点实现。
    ├── core/core_at.h
    │   # AT CPU 模型。TLM 闭环跑通后再写。
    └── nice/null_nice.h
        # 默认 NICE 空实现。写 CoreAT/NICE 时再补。
```

```text
src/
├── common/
│   ├── config.cpp
│   │   # 读取 configs/e203sim.json 和 configs/memory.json。
│   │   # 第一版可以先返回默认配置，再逐步接 JSON parser。
│   └── memory_map.cpp
│       # 实现地址区间 contains/decode。
│
├── platform/
│   └── e203_platform.cpp
│       # 根据 SimConfig 创建并连接 RouterAT、MemoryAT、StubPeripheral。
│       # 这里不写 CPU 行为，不写外设内部逻辑。
│
├── models/
│   ├── bus/router_at.cpp
│   │   # 实现 b_transport：地址 decode -> 转发到目标 socket。
│   ├── memory/memory_at.cpp
│   │   # 实现数组读写、越界检查、annotated delay。
│   └── peripherals/stub_peripheral.cpp
│       # 实现寄存器式 stub：write 保存，read 返回，默认 0。
│
└── sim/
    └── main.cpp
        # 宿主机仿真器入口。
        # 解析 --config、--bin、--load-addr，创建 E203Platform，启动 sc_start。
```

```text
tests/
├── unit/
│   └── memory_map_test.cpp
│       # 测 MemoryMap decode 是否符合 configs/memory.json。
└── integration/
    └── tlm_platform_smoke_test.cpp
        # 测 TLM 闭环：写/读 ITCM、DTCM、PPI stub，并检查非法地址。
```

```text
programs/
  # 放跑在 E203 里的裸机程序，不是宿主机测试。
  # 等 CoreAT 能取指执行后，再加入 hello、timer_irq、nice_smoke。
```

## 第一阶段实现顺序

```text
1. common/types.h
2. common/memory_map.h + src/common/memory_map.cpp
3. models/memory/memory_at.h + memory_at.cpp
4. models/peripherals/stub_peripheral.h + stub_peripheral.cpp
5. models/bus/router_at.h + router_at.cpp
6. platform/e203_platform.h + e203_platform.cpp
7. tests/integration/tlm_platform_smoke_test.cpp
8. sim/main.cpp 支持 --config configs/e203sim.json
```

第一阶段完成标志：

```text
make build
make test
make run
```

能证明最小平台已经活了：SystemC/TLM socket 能连接，Router 能解码地址，Memory/Stub 能响应访问。

```text
e203-systemc/
├── CMakeLists.txt
├── README.md
├── AGENTS.md
├── docs/
│   ├── hbirdv2_soc.pdf
│   ├── modeling_plan.md
│   ├── memory_map.md
│   ├── tlm_interfaces.md
│   ├── nice_model.md
│   └── ca_core.md
│
├── configs/
│   ├── e203_at.json
│   ├── e203_at_timed.json
│   ├── e203_ca_core.json
│   └── memory_map.json
│
├── include/
│   └── e203sim/
│       ├── common/
│       │   ├── config.hpp
│       │   ├── types.hpp
│       │   ├── memory_map.hpp
│       │   ├── trace.hpp
│       │   └── log.hpp
│       │
│       ├── isa/
│       │   ├── decoder.hpp
│       │   ├── instr.hpp
│       │   ├── executor.hpp
│       │   ├── csr.hpp
│       │   ├── trap.hpp
│       │   └── riscv_state.hpp
│       │
│       ├── interfaces/
│       │   ├── irq_if.hpp
│       │   ├── nice_if.hpp
│       │   ├── tick_if.hpp
│       │   ├── trace_if.hpp
│       │   └── tlm_ext.hpp
│       │
│       ├── platform/
│       │   ├── e203_platform.hpp
│       │   └── model_factory.hpp
│       │
│       └── models/
│           ├── core/
│           │   ├── core_base.hpp
│           │   ├── core_at.hpp
│           │   └── core_ca.hpp
│           ├── bus/
│           │   ├── router_at.hpp
│           │   └── router_ca.hpp
│           ├── memory/
│           │   ├── memory_at.hpp
│           │   └── memory_ca.hpp
│           ├── nice/
│           │   ├── null_nice.hpp
│           │   ├── nice_at.hpp
│           │   └── nice_ca.hpp
│           └── peripherals/
│               ├── clint.hpp
│               ├── plic.hpp
│               ├── uart_lite.hpp
│               └── stub_peripheral.hpp
│
├── src/
│   ├── common/
│   │   ├── config.cpp
│   │   ├── memory_map.cpp
│   │   ├── trace.cpp
│   │   └── log.cpp
│   │
│   ├── isa/
│   │   ├── decoder.cpp
│   │   ├── executor.cpp
│   │   ├── csr.cpp
│   │   └── trap.cpp
│   │
│   ├── platform/
│   │   ├── e203_platform.cpp
│   │   └── model_factory.cpp
│   │
│   ├── models/
│   │   ├── core/
│   │   │   ├── core_at.cpp
│   │   │   ├── core_ca.cpp
│   │   │   └── ca/
│   │   │       ├── ifu.hpp
│   │   │       ├── ifu.cpp
│   │   │       ├── exu.hpp
│   │   │       ├── exu.cpp
│   │   │       ├── lsu.hpp
│   │   │       ├── lsu.cpp
│   │   │       ├── commit.hpp
│   │   │       ├── commit.cpp
│   │   │       ├── pipe_regs.hpp
│   │   │       └── core_ctrl.hpp
│   │   │
│   │   ├── bus/
│   │   │   ├── router_at.cpp
│   │   │   └── router_ca.cpp
│   │   │
│   │   ├── memory/
│   │   │   ├── memory_at.cpp
│   │   │   └── memory_ca.cpp
│   │   │
│   │   ├── nice/
│   │   │   ├── null_nice.cpp
│   │   │   ├── nice_at.cpp
│   │   │   └── nice_ca.cpp
│   │   │
│   │   └── peripherals/
│   │       ├── clint.cpp
│   │       ├── plic.cpp
│   │       ├── uart_lite.cpp
│   │       └── stub_peripheral.cpp
│   │
│   └── sim/
│       ├── main.cpp
│       ├── arg_parser.hpp
│       ├── arg_parser.cpp
│       ├── elf_loader.hpp
│       └── elf_loader.cpp
│
├── tests/
│   ├── unit/
│   │   ├── decoder_test.cpp
│   │   ├── csr_test.cpp
│   │   ├── memory_map_test.cpp
│   │   └── nice_if_test.cpp
│   ├── integration/
│   │   ├── boot_hello_test.cpp
│   │   ├── clint_timer_test.cpp
│   │   └── nice_smoke_test.cpp
│   └── golden/
│       └── trace_compare.py
│
├── programs/
│   ├── linker/
│   │   ├── ilm.ld
│   │   └── flash.ld
│   ├── baremetal/
│   │   ├── hello/
│   │   ├── timer_irq/
│   │   └── nice_smoke/
│   └── scripts/
│       ├── build_program.sh
│       └── elf2hex.py
│
├── scripts/
│   ├── run_at.sh
│   ├── run_ca.sh
│   ├── dump_trace.py
│   └── compare_trace.py
│
└── third_party/
    └── README.md

```