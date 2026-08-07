# Feasibility and positioning

Research date: 2026-08-07
Decision: **GO, with a narrow embedded evidence-pipeline scope**

## The unmet integration problem

The individual parts are not novel: DSP libraries filter and transform samples,
recording formats store messages, telemetry products upload diagnostics, and edge-AI
platforms collect training data. The recurring engineering cost lies between them:
teams repeatedly define time and sequence semantics, reshape types, batch samples,
manage bounded retention, report loss, resume transfer, and recreate device inputs on
a host. ChronoPipe is valuable only if it removes that integration work while keeping
the application algorithm and hardware adapters replaceable.

## Adjacent projects and the boundary

| Project | What it already does well | Why ChronoPipe should not copy it | Remaining gap addressed here |
|---|---|---|---|
| CMSIS-DSP | Optimized FIR, FFT, statistics, and fixed/floating kernels on Arm | Competing on kernel speed would waste effort | Scheduling, stream contracts, lineage, recording, transfer, and replay around kernels |
| MCAP | Durable, schema-aware, indexed multi-channel pub/sub files | It is the better long-term host archive and ecosystem interchange format | A smaller MCU-oriented block, fixed-slot flash commit protocol, processing graph, and bounded RAM path |
| Edge Impulse | Integrated collection, feature/model workflow, and deployment | ChronoPipe should not become a hosted ML studio | Offline, vendor-neutral evidence for filters, diagnostics, control decisions, and arbitrary inference |
| Memfault | Device logs, metrics, coredumps, and operational upload workflows | It is an observability service, not a numeric algorithm graph | Typed high-rate numeric streams and exact input-to-output derivation |

Official references: [CMSIS-DSP overview](https://arm-software.github.io/CMSIS_6/latest/DSP/index.html),
[CMSIS real FFT](https://arm-software.github.io/CMSIS-DSP/main/group__RealFFT.html),
[MCAP specification](https://mcap.dev/spec),
[Edge Impulse data forwarder](https://docs.edgeimpulse.com/tools/clis/edge-impulse-cli/data-forwarder),
and [Memfault MCU logging](https://docs.memfault.com/docs/mcu/logging).

The comparison is an architectural inference from those published scopes, not a claim
that the other projects cannot be extended.

## Why open source is meaningful

Evidence infrastructure benefits from inspectable wire contracts and recovery rules.
An open device core makes long-lived recordings readable without a vendor service,
allows algorithm and QA teams to share a replay implementation, and lets silicon,
RTOS, storage, and transport vendors contribute adapters. Apache-2.0 also permits
commercial firmware use while preserving a common conformance surface.

Open source does not remove product risk. Adoption depends on excellent examples,
measured memory/CPU costs, stable compatibility tests, and integrations with existing
DSP and host formats. The project should prefer a small trusted core over a wide but
shallow connector catalog.

## Go conditions

- A new user can connect a source, a transform graph, a recorder, and a transport
  without modifying core code.
- Every stored or transmitted gap is detectable; committed flash records survive
  interrupted writes; acknowledged records do not reappear after reboot.
- Device and host agree on CPB1 bytes, integer outputs, timing, and lineage.
- The reference 200 Hz × 8-channel path has bounded memory and acceptable overhead.
- At least two real applications reuse the same core with different algorithms and
  hardware adapters.

## Stop or pivot conditions

- Applications still need to fork the core to add an algorithm or backend.
- Metadata and lineage cost more RAM/flash than teams are willing to spend, with no
  configurable evidence tier.
- Most prospective users already run Linux/ROS and gain more from writing MCAP
  directly; in that segment ChronoPipe should become a thin MCU adapter or exporter.
- Demand is primarily hard-real-time motor control scheduling, fleet management, or
  cloud visualization. Those are different products and should not expand this core.

## First adoption experiment

Use two dissimilar pipelines: one multi-channel filtering/FFT/feature path and one
state or inference pre/post-processing path. Measure integration time, static RAM,
flash retention, CPU excluding the application kernel, recovery after injected write
failures, and first-difference quality in replay. Proceed to a stable V1 only if both
reuse the public interfaces without core edits.
