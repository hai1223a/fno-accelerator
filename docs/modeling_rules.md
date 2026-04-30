# Modeling Rules

本文档描述本项目中的 SystemC/TLM 建模规范。

## 1. Module 命名

SystemC module 使用小写加下划线命名，例如：

- `dtcm`
- `cpu`
- `e203_soc`

C++ class 可以使用小写加下划线，也可以使用项目既有风格，但同一目录内需要保持一致。

## 2. Socket 命名

initiator socket 建议命名为：

```cpp
tlm_utils::simple_initiator_socket<module> source2target_initiator_socket;
```

target socket 建议命名为：

```cpp
tlm_utils::simple_target_socket<module> source2target_target_socket;
```

模块的socket命名需要体现方向，例如：
- `lsu2dtcm_initiator_socket`
- `lsu2dtcm_target_socket`

当模块需要暴露子模块模块socket给外部使用时，例如core需要暴露lsu的socket给到dtcm，中间socket命名为：
- `core2dtcm_initiator_socket`

当模块需要暴露多个子模块socket给相同外部模块时，建议命名：
- `corelsu2dtcm_initiator_socket`
- `coreifu2dtcm_initiator_socket`

## 3. Trace 规则

在设计src中systemc模块时，需要在关键节点添加调试信息

- 对于sc_module类，使用SIM_INFO()宏
- 对于普通C++类，使用INFO()宏
- 对于可能是C++语法或者使用错误导致的问题，需要使用SIM_ASSERT()宏进行检查，例如使用空指针，文件无法打开等
- 对于芯片模型运行时可能引发的错误，需要使用SIM_ERROR()宏进行检查，例如非法指令，非法地址等