# Integration Test Images

This directory contains tiny bare-metal RISC-V programs for simulator bring-up.
They do not depend on AM, libc, newlib, crt0, or any runtime library.

Build from the repository root:

```bash
make test-bin
```

Generated files are placed under `build/tests/integration/bin/`:

- `*.elf`: linked ELF image
- `*.bin`: raw binary for `bin_path`
- `*.txt`: objdump disassembly

The default toolchain prefix is `riscv64-unknown-linux-gnu-`. Override it with:

```bash
make test-bin RISCV_CROSS_COMPILE=riscv64-unknown-elf-
```

