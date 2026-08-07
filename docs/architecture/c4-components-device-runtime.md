# Device runtime components

```mermaid
C4Component
  title ChronoPipe V1 Device Firmware Components

  System_Ext(sensorHardware, "Sensors and ADC", "Physical sample sources")
  System_Ext(remoteEndpoint, "Remote Endpoint", "Receives evidence blocks")

  Container_Boundary(deviceFirmware, "Device Firmware") {
    Component(sourceAdapter, "Source Adapter", "Platform C", "Converts driver batches into source frames")
    Component(frameBuilder, "Frame Builder", "Portable C99", "Assigns sequence, monotonic time and quality")
    Component(streamGraph, "Static Stream Graph", "Portable C99", "Executes validated deterministic stages")
    Component(transformStages, "Transform Stages", "Portable C99 and DSP adapters", "Selects, converts, decimates and windows")
    Component(algorithmAdapter, "Algorithm Adapter", "Stable C ABI", "Calls product algorithms and emits derived streams")
    Component(blockRecorder, "CPB1 Recorder", "Portable C99", "Packs selected streams into sealed blocks")
    Component(blockStore, "Block Stores", "RAM ring and raw flash", "Retains acknowledged and pending evidence")
    Component(transportScheduler, "Transport Scheduler", "Platform adapters", "Streams live data and resumes batch upload")
    Component(diagnostics, "Diagnostics", "Portable C99", "Publishes loss, timing, saturation and recovery events")
  }

  Rel(sensorHardware, sourceAdapter, "Delivers acquired samples", "Driver callback")
  Rel(sourceAdapter, frameBuilder, "Submits bounded batches", "Frame view")
  Rel(frameBuilder, streamGraph, "Publishes ordered source frames", "C API")
  Rel(streamGraph, transformStages, "Executes configured transforms", "Static plan")
  Rel(transformStages, algorithmAdapter, "Delivers frames or windows", "Borrowed view")
  Rel(algorithmAdapter, streamGraph, "Emits derived streams", "Emitter API")
  Rel(streamGraph, blockRecorder, "Taps selected streams", "Borrowed view")
  Rel(blockRecorder, blockStore, "Appends sealed blocks", "CPB1")
  Rel(blockRecorder, transportScheduler, "Offers live blocks", "Bounded queue")
  Rel(blockStore, transportScheduler, "Supplies pending blocks", "Store API")
  Rel(transportScheduler, remoteEndpoint, "Uploads and receives acknowledgements", "Adapter protocol")
  Rel(diagnostics, streamGraph, "Publishes diagnostic events", "Event API")

  UpdateLayoutConfig($c4ShapeInRow="3", $c4BoundaryInRow="1")
```

Legend: components are linked modules inside Device Firmware. Source, graph, transforms,
and algorithms form the real-time data plane; recorder, store, and transport form the
evidence I/O plane connected through fixed block ownership.
