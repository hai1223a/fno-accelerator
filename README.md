# SystemC Chip Model

## 项目简介

本项目为e203_hbirdv2的SystemC模型，重点保证了以下方面的行为：
- 模块结构连接
- 地址映射
- e203 cpu的细粒度时序
- e203 soc的粗粒度时序
本项目不是 RTL 实现，也不保证 cycle-accurate 行为。

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
详细见[此处](docs/project.md)
