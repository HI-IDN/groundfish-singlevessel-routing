.PHONY: all demo demo-msvc cap matheur matcapheur matcapmutheur matcapmut_v2 matcapmutheur_v3 utils clean db

CC ?= /mingw64/bin/cc
MSVC ?= cl

# Output directories (relative to src/)
BIN_DIR ?= ../bin
LIB_DIR ?= ../lib

# Detect OS
UNAME_S := $(shell uname -s)

# Robust Windows/MSYS/CYGWIN detection: prefer MSYSTEM env var when present
# IS_WINDOWS will be '1' on MSYS/MINGW/CYGWIN or when OS=Windows_NT
IS_WINDOWS := $(shell if [ -n "$$MSYSTEM" ]; then echo 1; elif echo "$(UNAME_S)" | grep -E -q "MINGW|MSYS|CYGWIN" 2>/dev/null; then echo 1; elif [ "x$(OS)" = "xWindows_NT" ]; then echo 1; fi)
# Fallback for this repo's typical Windows/MSYS setup: detect installed Windows Gurobi tree.
ifeq ($(strip $(IS_WINDOWS)),)
ifneq ($(wildcard /c/gurobi1301/win64/include/gurobi_c.h),)
IS_WINDOWS := 1
endif
endif

# Fallback: if IS_WINDOWS contains '1' it'll be considered true

# -------- Gurobi --------
# Adjust this if your Linux install lives elsewhere (common: /opt/gurobiXXXX/linux64)
ifeq ($(UNAME_S),Darwin)
GUROBI_HOME ?= /Library/gurobi1300/macos_universal2
else
ifeq ($(IS_WINDOWS),1)
GUROBI_HOME ?= /c/gurobi1301/win64
else
GUROBI_HOME ?= /opt/gurobi1300/linux64
endif
endif

GUROBI_INC = -I$(GUROBI_HOME)/include
ifeq ($(IS_WINDOWS),1)
GUROBI_LIB = -L$(GUROBI_HOME)/lib -lgurobi130
WIN_LINK_FLAGS = -Wl,--enable-auto-import
else
GUROBI_LIB = -L$(GUROBI_HOME)/lib -lgurobi130 -Wl,-rpath,$(GUROBI_HOME)/lib
WIN_LINK_FLAGS =
endif
GUROBI_HOME_WIN ?= C:/gurobi1301/win64
GUROBI_INC_WIN = /I"$(GUROBI_HOME_WIN)/include"
GUROBI_LIB_WIN = "$(GUROBI_HOME_WIN)/lib/gurobi130.lib"

# -------- utils shared lib + rpath / windows dll + import lib --------
ifeq ($(UNAME_S),Darwin)
    UTILS_SO     = $(LIB_DIR)/libutils.dylib
    UTILS_RPATH  = -Wl,-rpath,@loader_path/../lib
    UTILS_IMPLIB =
else
    ifeq ($(IS_WINDOWS),1)
        # MSYS2 / MinGW / Cygwin: produce utils.dll and an import lib libutils.dll.a so -lutils works
        UTILS_SO     = $(LIB_DIR)/utils.dll
        UTILS_IMPLIB = $(LIB_DIR)/libutils.dll.a
        UTILS_RPATH  =
    else
        UTILS_SO     = $(LIB_DIR)/libutils.so
        # Use $$ to escape $ for make; the linker will see $ORIGIN at runtime.
        UTILS_RPATH  = -Wl,-rpath,'$$ORIGIN/../lib'
        UTILS_IMPLIB =
    endif
endif

# compute the platform-appropriate shared build flags once so recipes are simple
ifeq ($(IS_WINDOWS),1)
SHARED_FLAGS := -O3 -shared
OUT_IMPLIB_FLAGS := -Wl,--out-implib,$(UTILS_IMPLIB)
# copy the produced DLL into bin/ so the exe finds it at runtime (no-op on POSIX)
POST_COPY := mkdir -p $(BIN_DIR) && cp -f $(UTILS_SO) $(BIN_DIR)/ || true
else
SHARED_FLAGS := -O3 -fPIC -shared
OUT_IMPLIB_FLAGS :=
POST_COPY := true
endif

UTILS_LIB = -L$(LIB_DIR) -lutils $(UTILS_RPATH)

# -------- Targets --------
all: demo cap matheur matcapheur matcapmutheur matcapmut_v2 matcapmutheur_v3 utils db

demo: main.c utils
	mkdir -p $(BIN_DIR)
	$(CC) main.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) $(WIN_LINK_FLAGS) -lsqlite3 -lm -o $(BIN_DIR)/demo

# Windows/MSVC build path for the legacy demo.
# This links directly against Gurobi's .lib and compiles utils.c into the exe.
demo-msvc: main.c utils.c
	mkdir -p $(BIN_DIR)
	$(MSVC) /nologo /O2 /Fe$(BIN_DIR)/demo_legacy.exe main.c utils.c $(GUROBI_INC_WIN) $(GUROBI_LIB_WIN)

cap: capacity.c utils
	mkdir -p $(BIN_DIR)
	$(CC) capacity.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) $(WIN_LINK_FLAGS) -lm -o $(BIN_DIR)/cap

matheur: matheur.c utils
	mkdir -p $(BIN_DIR)
	$(CC) matheur.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) $(WIN_LINK_FLAGS) -lm -o $(BIN_DIR)/matheur

matcapheur: matcapheur.c utils
	mkdir -p $(BIN_DIR)
	$(CC) matcapheur.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) $(WIN_LINK_FLAGS) -lm -o $(BIN_DIR)/matcapheur

matcapmutheur: matcapmutheur.c utils
	mkdir -p $(BIN_DIR)
	$(CC) matcapmutheur.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) $(WIN_LINK_FLAGS) -lm -o $(BIN_DIR)/matcapmutheur

matcapmut_v2: matcapmut_v2.c utils
	mkdir -p $(BIN_DIR)
	$(CC) matcapmut_v2.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) $(WIN_LINK_FLAGS) -lm -o $(BIN_DIR)/matcapmut_v2

matcapmutheur_v3: matcapmutheur_v3.c utils
	mkdir -p $(BIN_DIR)
	$(CC) matcapmutheur_v3.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) $(WIN_LINK_FLAGS) -lm -o $(BIN_DIR)/matcapmutheur_v3

utils: utils.c
	mkdir -p $(LIB_DIR)
	$(CC) $(SHARED_FLAGS) utils.c -lm -o $(UTILS_SO) $(OUT_IMPLIB_FLAGS)
	$(POST_COPY)

# add parser target to build parse_dat_to_sqlite and a convenience 'db' target
parse_dat_to_sqlite: parse_dat_to_sqlite.c utils
	mkdir -p $(BIN_DIR)
	$(CC) parse_dat_to_sqlite.c $(UTILS_LIB) -lsqlite3 -lm -o $(BIN_DIR)/parse_dat_to_sqlite
db: parse_dat_to_sqlite

clean:
	rm -f $(BIN_DIR)/demo $(BIN_DIR)/cap $(BIN_DIR)/matheur $(BIN_DIR)/matcapheur $(BIN_DIR)/matcapmutheur $(BIN_DIR)/matcapmut_v2 $(BIN_DIR)/matcapmutheur_v3 \
	      $(LIB_DIR)/libutils.so $(LIB_DIR)/libutils.dylib $(BIN_DIR)/parse_dat_to_sqlite
