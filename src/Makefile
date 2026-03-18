# Lightweight Makefile - CMake wrapper
# Build directory is at ../build (root level)

BUILD_DIR = ../build

COUNTRY_DB ?= ../dat/gsp_data.db
COUNTRY_ISLAND ?= ../dat/island.tsv
COUNTRY_DAT ?= ../dat/waypoints.dat
COUNTRY_PORT_FILE ?= ../dat/ports.dat
COUNTRY_BOAT_FILE ?= ../dat/boats.dat
COUNTRY_STATIONS_FILE ?= ../dat/stations.dat

.PHONY: all clean config build prepare-routing-data preprocessing survey export_survey init init_nn init_ge init_ci init_opt noport-opt plot-routes help

all: build

config:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release ../src

build: config
	@cd $(BUILD_DIR) && $(MAKE)

init: build

prepare-routing-data: config
	@cd $(BUILD_DIR) && $(MAKE) gsp_prepare_routing_data
	@cd $(BUILD_DIR) && ./common/gsp_prepare_routing_data \
		--db $(COUNTRY_DB) \
		--coastline-file $(COUNTRY_ISLAND) \
		--waypoint-file $(COUNTRY_DAT) \
		--port-file $(COUNTRY_PORT_FILE) \
		--boat-file $(COUNTRY_BOAT_FILE) \
		--stations-file $(COUNTRY_STATIONS_FILE)

preprocessing: prepare-routing-data survey

survey: build
	@cd $(BUILD_DIR) && $(MAKE) survey

init_nn: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_nn

init_ge: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_ge

init_ci: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_ci

noport-opt: build
	@mkdir -p ../sol/opt
	@bash -lc 'cd "$(BUILD_DIR)" && set -o pipefail && PATH="$$GUROBI_HOME/bin:$$PATH" $(MAKE) run_noport_opt 2>&1 | tee ../sol/opt/noport.log'

init_opt: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_opt

plot-routes:
	@bash -lc 'set -euo pipefail; cd ..; \
	for f in $$(find sol -name "*.json"); do \
		png="$${f%.json}.png"; \
		if [ -f "$$png" ] && [ "$$png" -nt "$$f" ]; then \
			echo "Skipping $$f (png is up to date)"; \
		else \
			Rscript R/plot_survey_route.R "$$f"; \
		fi; \
	done'

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@echo "Cleaning previous data generation"
	@rm -rf $(COUNTRY_DB)
	@echo "Build directory removed"

reconfigure: clean config
	@echo "Reconfigured"

help:
	@echo ""
	@echo "GSP Solver - Lightweight Makefile"
	@echo "=================================="
	@echo ""
	@echo "Build directory: ../build (root level)"
	@echo ""
	@echo "Data Preparation:"
	@echo "  make prepare-routing-data"
	@echo ""
	@echo "Phase 0: Initilization"
	@echo "  make init_nn"
	@echo "  make init_ge"
	@echo "  make init_ci"
	@echo "  make noport-opt"
	@echo "  make init_opt"
	@echo ""
	@echo "Phase 1: Sweep"
	@echo ""
	@echo "Other targets:"
	@echo "  make build"
	@echo "  make survey"
	@echo "  make plot-routes"
	@echo "  make clean"
	@echo "  make reconfigure"
