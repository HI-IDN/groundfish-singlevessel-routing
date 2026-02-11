# common/Makefile - Data preparation modules

CC = gcc
CFLAGS = -Wall -Wextra -O2 -I../include
LIB_DIR = ../../lib
BIN_DIR = ../../bin
BUILD_DIR = build

# Library linking - use libutils which contains DistanceLink
UTILS_LIB = -L$(LIB_DIR) -lutils
SQLITE_LIB = -lsqlite3

ifeq ($(OS),Windows_NT)
    # Windows: need DLL in same directory or in PATH
    UTILS_RPATH =
else
    # Linux/Mac: use rpath to find library
    UTILS_RPATH = -Wl,-rpath,'$$ORIGIN/../../lib'
endif

# Source files
SRCS = dat_parser.c distance.c logging.c sql_utils.c
OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o)

# Data files
DAT_FILE = ../../dat/data2023spring.dat
DB_FILE = ../../dat/gsp_data.db

# Executable - data preparation utility
PREP_PROG = $(BUILD_DIR)/prep_data

# Main targets
.PHONY: all clean run prep

all: $(PREP_PROG)
	@echo "✓ Built prep_data utility"

# Data preparation utility
$(PREP_PROG): prep_data.c $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ prep_data.c $(OBJS) \
		$(UTILS_LIB) $(UTILS_RPATH) $(SQLITE_LIB) -lm
	@echo "✓ Linked $@"

# Parse and prepare data
prep: $(PREP_PROG)
	@echo ""
	@echo "=== Preparing Data ==="
	@echo "  Input: $(DAT_FILE)"
	@echo "  Database: $(DB_FILE)"
	@echo ""
	@# Copy utils DLL to build directory on Windows (if it exists)
	@if [ -f "$(LIB_DIR)/utils.dll" ]; then cp -f "$(LIB_DIR)/utils.dll" $(BUILD_DIR)/ 2>/dev/null || true; fi
	@if [ -f "$(BIN_DIR)/utils.dll" ]; then cp -f "$(BIN_DIR)/utils.dll" $(BUILD_DIR)/ 2>/dev/null || true; fi
	cd $(BUILD_DIR) && ./prep_data ../$(DAT_FILE) ../$(DB_FILE)

# Force recreate database
prep-force: $(PREP_PROG)
	@echo ""
	@echo "=== Preparing Data (FORCE RECREATE) ==="
	@echo "  Input: $(DAT_FILE)"
	@echo "  Database: $(DB_FILE)"
	@echo ""
	@# Copy utils DLL to build directory on Windows (if it exists)
	@if [ -f "$(LIB_DIR)/utils.dll" ]; then cp -f "$(LIB_DIR)/utils.dll" $(BUILD_DIR)/ 2>/dev/null || true; fi
	@if [ -f "$(BIN_DIR)/utils.dll" ]; then cp -f "$(BIN_DIR)/utils.dll" $(BUILD_DIR)/ 2>/dev/null || true; fi
	cd $(BUILD_DIR) && ./prep_data --force ../$(DAT_FILE) ../$(DB_FILE)

# Legacy alias
run: prep

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Compile object files
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned common utilities (removed build/)"




