# Lightweight Makefile - CMake wrapper
# Build directory is at ../build (root level)

BUILD_DIR = ../build
CONFIG_YAML = ../config/gsp_solver.yaml
CONFIG_L2SEG_VALUES = $(shell sed -n 's/^[[:space:]]*l2seg_values:[[:space:]]*\[\(.*\)\][[:space:]]*.*/\1/p' $(CONFIG_YAML) | tr ',' ' ')
ROUTE_JSONS = $(shell find ../sol -name "*.json")
ROUTE_PNGS = $(patsubst %.json,%.png,$(ROUTE_JSONS))

COUNTRY_DB ?= ../dat/gsp.db
COUNTRY_ISLAND ?= ../dat/island.tsv
COUNTRY_DAT ?= ../dat/waypoints.dat
COUNTRY_PORT_FILE ?= ../dat/ports.dat
COUNTRY_BOAT_FILE ?= ../dat/boats.dat
COUNTRY_STATIONS_FILE ?= ../dat/stations.dat

NN_INIT_JSON = ../sol/nn/init.json
L2SEG ?=

.PHONY: all clean config build prepare-routing-data preprocessing survey export_survey init init_nn init_ge init_ci init_opt noport-opt sweep_nn plot plot-country plot-overview plot-routes help

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

sweep_nn: build
	@mkdir -p ../sol/nn
	@bash -lc 'set -euo pipefail; \
		init_json="$(NN_INIT_JSON)"; \
		db="$(COUNTRY_DB)"; \
		l2seg="$(L2SEG)"; \
		configured_l2seg="$(CONFIG_L2SEG_VALUES)"; \
		if [ -n "$$l2seg" ]; then \
			run_values="$$l2seg"; \
		else \
			run_values="$$configured_l2seg"; \
		fi; \
		if [ -z "$$run_values" ]; then \
			echo "No L2SEG values configured in $(CONFIG_YAML)"; \
			exit 1; \
		fi; \
		if [ ! -f "$$init_json" ]; then \
			echo "NN init missing -> generating $$init_json"; \
			cd "$(BUILD_DIR)" && $(MAKE) run_init_nn; \
		elif [ "$$db" -nt "$$init_json" ]; then \
			echo "Database newer than $$init_json -> regenerating NN init"; \
			cd "$(BUILD_DIR)" && $(MAKE) run_init_nn; \
		else \
			echo "Reusing existing $$init_json"; \
		fi; \
		for value in $$run_values; do \
			value=$$(echo "$$value" | xargs); \
			output="../sol/nn/sweep_$${value}.json"; \
			log="../sol/nn/sweep_$${value}.log"; \
			echo "Running NN sweep for L2SEG=$$value -> $$output"; \
			cd "$(BUILD_DIR)" && set -o pipefail && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_gurobi --mode sweep --strategy nn --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "$$init_json" --output "$$output" --l2seg "$$value" 2>&1 | tee "$$log"; \
		done'

plot-routes: $(ROUTE_PNGS)

../sol/%.png: ../sol/%.json
	@cd .. && Rscript R/plot_survey_route.R "$(patsubst ../%,%,$<)" "$(patsubst ../%,%,$@)"

plot-country:
	@bash -lc 'cd .. && Rscript R/plot_country.R dat/gsp.db dat/coastline_waypoints_ports.png'

plot-overview:
	@bash -lc 'cd .. && Rscript R/plot_survey_overview.R'

plot: plot-country plot-overview plot-routes

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
	@echo "  make sweep_nn           # run all L2SEG values from config"
	@echo "  make sweep_nn L2SEG=120 # run one configured L2SEG value"
	@echo ""
	@echo "Other targets:"
	@echo "  make build"
	@echo "  make survey"
	@echo "  make plot"
	@echo "  make plot-country"
	@echo "  make plot-overview"
	@echo "  make plot-routes"
	@echo "  make clean"
	@echo "  make reconfigure"
