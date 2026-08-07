# ChronoPipe

[English](README.md)

ChronoPipe 是一个嵌入式优先的时序处理与算法证据管线。它以一份版本化契约打通采集、确定性转换、窗口算法、有界存储、实时流、可靠批量上传和主机回放。

项目面向 MCU 与小型边缘设备上的传感器融合、滤波、工业状态判断、故障诊断、电机控制中的非实时决策，以及边缘推理前后处理。

## 设计目标

- 启动完成后不再进行堆分配，支持 50-200 Hz 的多通道数值流。
- 时间、序列、质量、类型、单位和派生关系均具有明确语义。
- 内置通道选择、仿射变换、量化、降采样、移动均值、窗口聚合、参考频谱和峰值频率阶段。
- 应用算法通过小型 C ABI 接入，不与存储或 I/O 耦合。
- RAM 环、Flash 环、文件和批量传输共用紧凑的 CPB1 二进制块。
- 提供低延迟实时传输，以及可确认、可续传的批量传输。
- 设备与主机复用可移植 C 算法核心，支持可信回放。

## 仓库状态

ChronoPipe 按公开的 V1 规范构建。在首个稳定版本前，接口仍可能调整。请阅读 [V1 设计](docs/v1-design.md) 与 [路线图](ROADMAP.md)。

## 目录结构

```text
core/c/             可移植 C99 运行时
host/python/        CPB1 工具：采集、回放、比较与导出
ports/              平台适配说明
spec/               公开规范与模式
examples/           可运行的参考管线
tests/              C、Python、黄金数据与故障注入测试
docs/               架构和集成指南
```

## 构建

```sh
cmake -S . -B build -DCHRONOPIPE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

主机工具需要 Python 3.9 或更高版本，且没有强制运行时依赖：

```sh
python -m pip install -e .
chronopipe --help
```

建议从[集成指南](docs/integration.md)和可运行的[八通道管线示例](examples/host_pipeline/README.zh-CN.md)开始。

## 范围边界

ChronoPipe 负责数据与证据编排；它不替代 DSP 库、传感器驱动、硬实时控制循环、设备管理或云平台。

针对这一细分范围的可行性结论是有条件的 **GO**。详见[可行性与定位](docs/feasibility.md)，其中包含证据、推进条件和停止条件。

## 许可证

Apache-2.0，见 [LICENSE](LICENSE)。
