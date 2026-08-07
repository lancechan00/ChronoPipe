# Host tools

[中文文档](README.zh-CN.md)

The `chronopipe` Python package is a zero-runtime-dependency toolbox for CPB1 evidence.
It targets Python 3.9 or newer.

```sh
python -m pip install -e .
chronopipe inspect capture.cpb --json
chronopipe export capture.cpb capture.csv --stream 1
chronopipe compare golden.cpb replayed.cpb --atol 1e-6 --rtol 1e-5
chronopipe replay capture.cpb framed.bin
chronopipe capture framed.bin recovered.cpb
```

`replay` and `capture` use COBS frames terminated by a zero byte. Each decoded frame is
exactly one CPB1 record. This small framing layer is suitable for UART, USB CDC, TCP, or
test files; hardware-specific port opening intentionally remains in the application.
