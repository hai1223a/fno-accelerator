# AGENTS.md

## Project Goal

本项目是一个基于 SystemC/TLM 的芯片级建模项目，是对开源RISC-V CPU e203_hbirdv2及其协处理器进行建模。 模型为混合粒度层次，CPU、协处理器等核心部分建立CA模型，SoC外设等外围部分建立AT模型，基于transaction进行通信。

项目目标是建立清晰、可维护、可扩展的 architectural model，不是建立RTL层细粒度模型。

## Architectural Requirements

本项目是针对e203_hbirdv2的建模，因此需要遵循其架构要求，包括但不限于：

- RISC-V指令集架构
- [e203_hbirdv2的手册](docs/hbirdv2_soc.pdf)
- [e203_hbirdv2的rtl代码](third_party/e203_hbirdv2/rtl)
- [e203_hbirdv2的项目文档](third_party/e203_hbirdv2/doc)

## Modeling Scope

本项目主要使用以下建模层级：

- SystemC module 建模结构层次
- TLM2.0 建模 transaction-level communication
- Approximately-timed model 用于描述粗粒度时序
- Cycle-accurate model 用于描述细粒度时序
- 必要时使用 sc_signal / sc_fifo / sc_event 建模局部同步行为

除非明确要求，不应该忽略任何模块的时序，也不应该进行信号线粒度的建模。

## Coding Rules

- 使用 C++17。
- 使用 SystemC 和 TLM2.0。
- 模块接口应尽量清晰，避免隐藏复杂副作用。
- Header 文件只放声明和简单 inline 代码。
- Implementation 放在 `.cpp` 文件中。
- 避免一次性大规模重构。
- 修改已有模块时，优先保持原有命名风格和目录结构。
- 不要随意改变 socket 类型，例如不要无理由将 `simple_target_socket` 改成 `simple_initiator_socket`。
- 不要混用 blocking transport 和 non-blocking transport，除非说明原因。

## TLM Rules

修改 TLM 相关代码时，需要特别注意：

- `b_transport` 适合 Approximately-timed model。
- `nb_transport_fw` / `nb_transport_bw` 适合 Cycle-accurate model。
- 需要明确说明 phase 变化，例如 `BEGIN_REQ`、`END_REQ`、`BEGIN_RESP`、`END_RESP`。
- 需要明确说明 delay 是覆盖、累加，还是作为 annotation 返回。
- `tlm_generic_payload` 必须正确设置：
  - command
  - address
  - data pointer
  - data length
  - byte enable pointer
  - streaming width
  - response status
- target 收到 transaction 后必须设置 response status。
- 不要让 payload 指针生命周期早于 transaction 完成。

## Timing Rules

- 如果模型是 Approximately-timed，应明确使用 `b_transport` 和 annotated delay。
- 如果模型是 Cycle-accurate，应使用 `nb_transport_fw` 和 `nb_transport_bw` 以及累加延迟。
- 如果模型需要 req/resp 分离，应使用 non-blocking TLM。

## Build Commands

构建项目：
```bash
make build -j8
```

运行模型：
```bash
make run -j8
```

清理模型：
```bash
make clean
```

运行测试:
```bash
make test-模块名
```

## Debugging Rules

修复 bug 时，先说明 root cause。
优先给出最小修改。
不要改动无关模块。
如果涉及 timing 行为变化，需要说明修改前后的 transaction 时序。
如果涉及 memory access，需要说明 address mapping、data length、endianness 和 byte enable 行为。

## Do Not Touch

除非用户明确要求，不要修改：
build/
.env
生成的日志文件
波形文件
第三方库源码
已经确认可工作的 build system

## Testing Rules

开发或修改某个模块时，不要只运行完整系统 demo。优先添加或更新对应的 component test。

测试分层如下：

- unit test：测试纯 C++ 逻辑，例如 address decoder、register file。
- component test：单独实例化一个 SystemC/TLM 模块，并用 testbench initiator 发 transaction。
- integration test：测试多个模块连接后的行为，例如 bus + memory + peripheral。
- scenario test：运行完整 SoC 场景，例如 boot、DMA copy、video pipeline。

修改 TLM target 时，至少测试：

- normal read/write
- invalid address
- response status
- data length
- byte enable
- delay annotation

修改 non-blocking TLM 模块时，至少测试：

- phase order
- response cycle
- outstanding behavior
- payload lifetime
- error response path

后续添加测试时，按照scripts/mk/test-(modulename).mk的格式添加启动脚本

## doc rules

项目结构格式参考文档[docs/project.md](docs/project.md)
建模规范参考文档[docs/modeling_rules.md](docs/modeling_rules.md)