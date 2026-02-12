# Lightweight Makefile - CMake wrapper
# Build directory is at ../build (root level)

BUILD_DIR = ../build

.PHONY: all clean config build preprocessing init init_nn init_ge init_ci init_opt help

# Default target
all: build

# Configure CMake (only if not already configured)
config:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release ../src-refactor

# Build everything
build: config
	@cd $(BUILD_DIR) && $(MAKE)

# Alias for build
init: build

# Data preprocessing
preprocessing: config
	@cd $(BUILD_DIR) && $(MAKE) preprocess

# Run Phase 0 strategies
init_nn: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_nn

init_ge: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_ge

init_ci: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_ci

init_opt: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_opt

# Clean build directory
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@echo "✓ Build directory removed"

# Reconfigure (clean + config)
reconfigure: clean config
	@echo "✓ Reconfigured"

# Help message
help:
	@echo ""
	@echo "GSP Solver - Lightweight Makefile"
	@echo "=================================="
	@echo ""
	@echo "Build directory: ../build (root level)"
	@echo ""
	@echo "Quick Start:"
	@echo "  make                    Build gsp_init solver"
	@echo "  make preprocessing      Parse .dat and build database"
	@echo "  make init_nn            Run Nearest Neighbor (NN)"
	@echo ""
	@echo "All Targets:"
	@echo "  make build              Build all targets"
	@echo "  make config             Configure CMake (if needed)"
	@echo "  make preprocessing      Step 1: Parse data"
	@echo "  make init / make        Step 2: Build Phase 0 solver"
	@echo "  make init_nn            Run NN strategy (IMPLEMENTED)"
	@echo "  make init_ge            Run GE strategy (not yet implemented)"
	@echo "  make init_ci            Run CI strategy (not yet implemented)"
	@echo "  make init_opt           Run OPT strategy (not yet implemented)"
	@echo "  make clean              Remove build directory"
	@echo "  make reconfigure        Clean and reconfigure CMake"
	@echo ""
	@echo "Or use CMake directly:"
	@echo "  cd ../build"
	@echo "  cmake -DCMAKE_BUILD_TYPE=Release ../src-refactor"
	@echo "  make"
	@echo "  make run_init_nn"
	@echo ""

