.PHONY: all demo demo_scip cap matheur matcapheur utils clean

CC ?= clang

# Output directories (relative to src/)
BIN_DIR ?= ../bin
LIB_DIR ?= ../lib

# Detect OS
UNAME_S := $(shell uname -s)

# -------- Gurobi --------
# Adjust this if your Linux install lives elsewhere (common: /opt/gurobiXXXX/linux64)
ifeq ($(UNAME_S),Darwin)
GUROBI_HOME ?= /Library/gurobi1300/macos_universal2
else
GUROBI_HOME ?= /opt/gurobi1300/linux64
endif

GUROBI_INC = -I$(GUROBI_HOME)/include
GUROBI_LIB = -L$(GUROBI_HOME)/lib -lgurobi130 -Wl,-rpath,$(GUROBI_HOME)/lib

# -------- utils shared lib + rpath --------
ifeq ($(UNAME_S),Darwin)
UTILS_SO     = $(LIB_DIR)/libutils.dylib
UTILS_RPATH  = -Wl,-rpath,@loader_path/../lib
else
UTILS_SO     = $(LIB_DIR)/libutils.so
# Use $$ to escape $ for make; the linker will see $ORIGIN at runtime.
UTILS_RPATH  = -Wl,-rpath,'$$ORIGIN/../lib'
endif

UTILS_LIB = -L$(LIB_DIR) -lutils $(UTILS_RPATH)

# -------- SCIP --------
SCIP_HOME ?= $(HOME)/.local/scipoptsuite-10.0.0
SCIP_CONFIG ?= $(SCIP_HOME)/bin/scip-config
SCIP_FLAGS := $(shell $(SCIP_CONFIG) --cflags --libs 2>/dev/null)

ifeq ($(strip $(SCIP_FLAGS)),)
SCIP_FLAGS = -I$(SCIP_HOME)/include -L$(SCIP_HOME)/lib -lscip
endif

# -------- Targets --------
all: demo demo_scip cap matheur matcapheur utils

demo: main.c utils
	mkdir -p $(BIN_DIR)
	$(CC) main.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) -lm -o $(BIN_DIR)/demo

demo_scip: main_scip.c utils
	mkdir -p $(BIN_DIR)
	$(CC) main_scip.c $(SCIP_FLAGS) $(UTILS_LIB) -lm -o $(BIN_DIR)/demo_scip

cap: capacity.c utils
	mkdir -p $(BIN_DIR)
	$(CC) capacity.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) -lm -o $(BIN_DIR)/cap

matheur: matheur.c utils
	mkdir -p $(BIN_DIR)
	$(CC) matheur.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) -lm -o $(BIN_DIR)/matheur

matcapheur: matcapheur.c utils
	mkdir -p $(BIN_DIR)
	$(CC) matcapheur.c $(GUROBI_INC) $(GUROBI_LIB) $(UTILS_LIB) -lm -o $(BIN_DIR)/matcapheur

utils: utils.c
	mkdir -p $(LIB_DIR)
	$(CC) -O3 -fPIC -shared utils.c -lm -o $(UTILS_SO)

clean:
	rm -f $(BIN_DIR)/demo $(BIN_DIR)/demo_scip $(BIN_DIR)/cap $(BIN_DIR)/matheur $(BIN_DIR)/matcapheur \
	      $(LIB_DIR)/libutils.so $(LIB_DIR)/libutils.dylib
