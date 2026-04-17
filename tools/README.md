# `tools/`

Pre-flight smoke-tests for the three native dependencies: SQLite, GEOS, and Gurobi.
Run these before the first build to confirm your environment is correctly set up.

```bash
make -C tools test-env
```

Each test compiles a small C program, links the target library, and runs a minimal
functional check. Output is `PASS` or `FAIL` with an exit code.

## Targets

| Target | Tests | What it checks |
|--------|-------|---------------|
| `test-sqlite` | `test_sqlite.c` | Open an in-memory DB, create a table, insert and read back a value |
| `test-geos` | `test_geos.c` | Init the GEOS reentrant context, build a polygon, verify `GEOSisValid_r` |
| `test-gurobi` | `test_gurobi.c` | Load a Gurobi environment and confirm the license is reachable |
| `test-env` | all three | Runs the above in sequence (default target) |

## Configuration

Gurobi path defaults to `/opt/gurobi1300/linux64` (Linux) or
`/Library/gurobi1300/macos_universal2` (macOS). Override with:

```bash
make -C tools test-env GUROBI_HOME=/path/to/gurobi
```

On Windows/MSYS2 the Makefile detects the environment automatically.

