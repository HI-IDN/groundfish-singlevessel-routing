Repository overview
===================

This folder is the root for the build and test workflow described below. It contains the survey-routing matheuristic codebase and a small, pure-C toolchain for preprocessing, initialization, and sweep evaluation.

This README documents the environment checks you should run first, how to build the minimal smoke-tests (SQLite + Gurobi), and how to proceed once the environment is confirmed. These checks ensure the system has the required C development libraries and solver available before you perform larger refactors.

Layout
------
- `src/` - pure-C implementation and Makefile (build targets and tests live here).
- `src-old/` - previous source tree (kept as reference).
- `dat/` - raw .dat files (e.g. `singleboatdata2023spring.dat`).
- `sol/` - solutions and outputs produced by runs.
- `bin/` - legacy binaries (kept for compatibility).
- `tools/` - small helper programs and environment smoke-tests.
- `config/` - YAML experiment configuration files.

Prerequisites
-------------
You need a C toolchain (GCC/Clang), the SQLite development headers and library, and Gurobi (headers, libs and a valid license). These instructions assume Linux/macOS or Windows under MSYS2/WSL.

1) C toolchain
- Linux (Debian/Ubuntu): `sudo apt install build-essential`
- macOS (Homebrew): `brew install gcc`
- Windows (MSYS2/MinGW): use the MSYS2 package manager to install `mingw-w64-x86_64-gcc`.

2) SQLite (development headers)
- Linux (Debian/Ubuntu): `sudo apt install libsqlite3-dev`
- macOS (Homebrew): `brew install sqlite`
- Windows (MSYS2): `pacman -S mingw-w64-x86_64-sqlite3`

3) Gurobi
- Download and install Gurobi for your platform from https://www.gurobi.com/downloads/.
- Ensure you have a functioning license (for example, a `gurobi.lic` file or `GRB_LICENSE_FILE` pointing to a license).
- Set `GUROBI_HOME` to the installation directory. Example you provided in MSYS:
  ```bash
  export GUROBI_HOME="/c/gurobi1301/win64"
  export PATH="$GUROBI_HOME/bin:$PATH"
  export LD_LIBRARY_PATH="$GUROBI_HOME/lib:${LD_LIBRARY_PATH:-}"
  ```
- Ensure the compiler can find headers at `$GUROBI_HOME/include` and the linker at `$GUROBI_HOME/lib`.

Smoke-tests (verify environment)
--------------------------------
Two tiny C programs and a shell runner validate SQLite and Gurobi are available and linkable from your C toolchain.

Run the tests from this directory (the project root):

```bash
make -C tools test-env
```

Expected output (both tests must PASS):

- `SQLITE_TEST: PASS` — SQLite headers and library are usable.
- `GUROBI_TEST: PASS - status=<status> elapsed=<s> s` — Gurobi headers and basic env allocation succeed.

If those tests pass, you can proceed to building and running the full pipeline.

Troubleshooting
---------------
- If the sqlite test fails: ensure `sqlite3.h` is installed and the `-lsqlite3` library is available. On Debian/Ubuntu install `libsqlite3-dev`.
- If the Gurobi test fails: ensure `GUROBI_HOME` is set and points to your Gurobi installation (headers under `$GUROBI_HOME/include` and libs under `$GUROBI_HOME/lib`). Also verify your Gurobi license is valid.