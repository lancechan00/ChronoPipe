# Host pipeline example

This example runs the portable C99 runtime on synthetic eight-channel input. It branches
the raw stream into quantized evidence and a FIR-decimated processing path, then produces
moving means, overlapping windows, spectra, and peak-frequency features.

Build and run it with:

```sh
cmake -S . -B build
cmake --build build --config Release
./build/examples/host_pipeline/chronopipe_pipeline_example
```

On multi-configuration generators the executable is inside the `Release` directory.
