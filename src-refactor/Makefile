# Lightweight Makefile - CMake wrapper
# Build directory is at ../build (root level)

BUILD_DIR = ../build

COUNTRY_DB ?= ../dat/gsp_data.db
COUNTRY_ISLAND ?= ../dat/island.bin
COUNTRY_DAT ?= ../dat/data2023spring.dat
COUNTRY_PORT_FILE ?= ../dat/data2023spring.dat
COUNTRY_MIN ?= 40
COUNTRY_MAX ?= 60
COUNTRY_TARGET ?= 50

.PHONY: all clean config build preprocessing init init_nn init_ge init_ci init_opt export_survey country help

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

# Country bootstrap: infer coastline waypoints (~50 target) and import ports from a port file
country: config
	@cd $(BUILD_DIR) && $(MAKE) gsp_country
	@cd $(BUILD_DIR) && ./gsp_country \
		--db $(COUNTRY_DB) \
		--island-bin $(COUNTRY_ISLAND) \
		--dat $(COUNTRY_DAT) \
		--use-dat-waypoints --seed-hints-only \
		--port-file $(COUNTRY_PORT_FILE) \
		--min-points $(COUNTRY_MIN) --max-points $(COUNTRY_MAX) --target-points $(COUNTRY_TARGET)

# Run Phase 0 strategies
init_nn: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_nn

init_ge: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_ge

init_ci: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_ci

init_opt: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_opt

# Export survey to JSON for plotting
export_survey: build
	@echo "[Exporting survey to JSON]"
	@cd $(BUILD_DIR) && ./export_survey_json ../dat/gsp_data.db ../sol/survey
	@echo "✓ Survey routes exported to sol/survey/boat*.json"

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
	@echo "  make country            Build and run coastline+waypoint+port bootstrap (~50 waypoints)"
	@echo "  make preprocessing      Parse .dat and build database"
	@echo "  make export_survey      Export survey routes to JSON"
	@echo "  make init_nn            Run Nearest Neighbor (NN)"
	@echo ""
	@echo "All Targets:"
	@echo "  make build              Build all targets"
	@echo "  make config             Configure CMake (if needed)"
	@echo "  make preprocessing      Step 1: Parse data"
	@echo "  make country            Step 1a: Country bootstrap (coastline + waypoints + ports)"
	@echo "  make export_survey      Export all boat routes to JSON"
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
