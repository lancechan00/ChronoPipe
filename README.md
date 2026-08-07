# ChronoPipe

ChronoPipe is an embedded-first time-series processing and algorithm evidence pipeline.
It connects acquisition, deterministic transforms, windowed algorithms, bounded
recording, live streaming, reliable batch upload, and host replay through one
versioned contract.

The project targets sensor fusion, filtering, condition monitoring, fault diagnosis,
non-real-time motor decisions, and edge inference pre/post-processing on MCUs and
small edge devices.

## Design goals

- 50–200 Hz multi-channel numeric streams without heap allocation after startup.
- Explicit time, sequence, quality, type, unit, and derivation semantics.
- Built-in selection, affine conversion, quantization, decimation, moving mean,
  window aggregation, reference spectrum, and peak-frequency stages.
- Application algorithms behind a small C ABI instead of coupled to storage or I/O.
- A compact CPB1 binary block shared by RAM rings, flash rings, files, and batch transport.
- Low-latency live transport and acknowledged resumable batch transport.
- The same portable C algorithm core on device and host for trustworthy replay.

## Repository status

ChronoPipe is being built from its public V1 specification. Interfaces may change
until the first stable release. See [the V1 specification](docs/v1-design.md) and
[the roadmap](ROADMAP.md).

## Layout

```text
core/c/             portable C99 runtime
host/python/        CPB1 tools, capture, replay, compare, and export
ports/              platform adapters
spec/               normative public formats and schemas
examples/           runnable reference pipelines
tests/              C, Python, golden, and fault-injection tests
docs/               architecture and integration guides
```

## Build

```sh
cmake -S . -B build -DCHRONOPIPE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Host tools require Python 3.9 or newer and have no mandatory runtime dependency:

```sh
python -m pip install -e .
chronopipe --help
```

Start with the [integration guide](docs/integration.md) and the runnable
[eight-channel pipeline](examples/host_pipeline/README.md).

## Scope boundary

ChronoPipe orchestrates data and evidence. It does not replace DSP libraries,
sensor drivers, hard real-time control loops, device management, or a cloud platform.

The feasibility decision is a conditional **GO** for this narrow scope. See
[Feasibility and positioning](docs/feasibility.md) for the evidence and stop conditions.

## License

Apache-2.0. See [LICENSE](LICENSE).
