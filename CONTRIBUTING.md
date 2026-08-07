# Contributing

Thank you for helping make embedded algorithm evidence reproducible.

## Development

1. Read `AGENTS.md`, `docs/v1-design.md`, and the normative files in `spec/`.
2. Keep the C core C99, allocation-free after initialization, and independent of an RTOS.
3. Add tests for every contract, codec, scheduling, storage, or compatibility change.
4. Run CMake/CTest and the Python unittest suite before opening a pull request.
5. Update documentation and the changelog for user-visible behavior.

Do not submit private device identifiers, production captures, credentials, proprietary
algorithm coefficients, or source copied from another project.

Contributions are accepted under Apache-2.0 and must include a Developer Certificate
of Origin sign-off (`Signed-off-by:`).
