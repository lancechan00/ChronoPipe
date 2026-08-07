# 主机管线示例

[English](README.md)

此示例在合成的八通道输入上运行可移植 C99 运行时。原始流分支为量化证据路径和 FIR 降采样处理路径，随后产出移动均值、重叠窗口、频谱与峰值频率特征。

构建并运行：

```sh
cmake -S . -B build
cmake --build build --config Release
./build/examples/host_pipeline/chronopipe_pipeline_example
```

对于多配置生成器，可执行文件位于 `Release` 目录中。
