# Agent Guide

This repository welcomes AI-assisted contributions when they remain reviewable,
tested, and attributable to a human contributor.

## Non-negotiable constraints

- Never add private project names, absolute local paths, device identifiers, credentials,
  production data, or proprietary algorithm coefficients.
- Do not copy code from unrelated repositories or private workspaces.
- Preserve the public contracts in `spec/`; incompatible changes require a major version.
- Keep the portable core C99 and free of RTOS, filesystem, socket, or heap assumptions.
- No allocation, blocking I/O, flash erase, or network operation on the real-time path.
- Every data loss, saturation, reset, and discontinuity must be observable.
- Device and host replay must share the same algorithm implementation when equivalence matters.
- Use fixed-width integers, explicit little-endian encoding, checked arithmetic, and bounded buffers.
- Add or update tests with every behavior change.

## Working style

Prefer small modules with explicit ownership. Write design decisions before introducing
new abstraction layers. Treat green tests as evidence only for behavior they actually
cover. Run privacy and secret scans before handing work off.
