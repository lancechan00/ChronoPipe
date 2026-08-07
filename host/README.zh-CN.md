# 主机工具

[English](README.md)

`chronopipe` Python 包是一套零运行时依赖的 CPB1 证据工具箱，支持 Python 3.9 或更高版本。

```sh
python -m pip install -e .
chronopipe inspect capture.cpb --json
chronopipe export capture.cpb capture.csv --stream 1
chronopipe compare golden.cpb replayed.cpb --atol 1e-6 --rtol 1e-5
chronopipe replay capture.cpb framed.bin
chronopipe capture framed.bin recovered.cpb
```

`replay` 与 `capture` 使用以零字节结尾的 COBS 帧。每个解码帧恰好对应一条 CPB1 记录。该轻量封装适合 UART、USB CDC、TCP 或测试文件；具体硬件端口的打开方式有意留在应用层。
