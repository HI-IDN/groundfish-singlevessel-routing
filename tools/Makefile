# Makefile for tools smoke-tests (SQLite + Gurobi)

CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra
LDFLAGS ?=

# Detect OS
UNAME_S := $(shell uname -s)

# Robust Windows/MSYS/CYGWIN detection: prefer MSYSTEM env var when present
IS_WINDOWS := $(shell if [ -n "$$MSYSTEM" ]; then echo 1; elif echo "$(UNAME_S)" | grep -E -q "MINGW|MSYS|CYGWIN" 2>/dev/null; then echo 1; elif [ "x$(OS)" = "xWindows_NT" ]; then echo 1; fi)

# -------- Gurobi --------
# Adjust this if your Linux install lives elsewhere (common: /opt/gurobiXXXX/linux64)
ifeq ($(UNAME_S),Darwin)
GUROBI_HOME ?= /Library/gurobi1300/macos_universal2
else
GUROBI_HOME ?= /opt/gurobi1300/linux64
endif

GUROBI_INC = -I$(GUROBI_HOME)/include

# On Windows, DLL is in bin; on Linux/Mac, shared lib is in lib
ifeq ($(IS_WINDOWS),1)
    GUROBI_LIB = -L$(GUROBI_HOME)/lib -lgurobi130 -L$(GUROBI_HOME)/bin
    GUROBI_RPATH =
else
    GUROBI_LIB = -L$(GUROBI_HOME)/lib -lgurobi130 -Wl,-rpath,$(GUROBI_HOME)/lib
endif

.PHONY: all test-sqlite test-gurobi test-env clean
all: test-env

# Compile and run the sqlite smoke test
test-sqlite:
	@echo "Compiling sqlite test..."
	$(CC) $(CFLAGS) -o test_sqlite test_sqlite.c -lsqlite3 $(LDFLAGS)
	@echo "Running sqlite test..."
	./test_sqlite

# Compile and run the gurobi smoke test
test-gurobi:
ifndef GUROBI_HOME
	$(error GUROBI_HOME is not set)
endif
	@echo "Compiling gurobi test..."
	$(CC) $(CFLAGS) $(GUROBI_INC) -o test_gurobi test_gurobi.c $(GUROBI_LIB) -lgurobi130 $(LDFLAGS)
	@echo "Running gurobi test..."
	# Ensure runtime loader and MSYS PATH can find gurobi libs
	# On Windows: add both bin (DLL) and lib (import lib) directories
	# On Linux/Mac: add lib directory with rpath already compiled in
ifeq ($(IS_WINDOWS),1)
	LD_LIBRARY_PATH=$(GUROBI_HOME)/bin:$(GUROBI_HOME)/lib:$$LD_LIBRARY_PATH PATH=$(GUROBI_HOME)/bin:$(GUROBI_HOME)/lib:$$PATH ./test_gurobi
else
	LD_LIBRARY_PATH=$(GUROBI_HOME)/lib:$$LD_LIBRARY_PATH ./test_gurobi
endif

# Run both tests
test-env: test-sqlite test-gurobi
	@echo "Environment smoke-tests complete."

clean:
	@rm -f test_sqlite test_gurobi
	@echo "cleaned"

