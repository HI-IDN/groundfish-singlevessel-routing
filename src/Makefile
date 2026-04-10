# Lightweight Makefile - CMake wrapper
# Build directory is at ../build (root level)

BUILD_DIR = ../build
CONFIG_YAML = ../config/gsp_solver.yaml
ROUTE_JSONS = $(shell find ../sol -name "*.json")
ROUTE_PNGS = $(patsubst %.json,%.png,$(ROUTE_JSONS))

COUNTRY_DB ?= ../dat/gsp.db
COUNTRY_ISLAND ?= ../dat/island.tsv
COUNTRY_DAT ?= ../dat/waypoints.dat
COUNTRY_PORT_FILE ?= ../dat/ports.dat
COUNTRY_BOAT_FILE ?= ../dat/boats.dat
COUNTRY_STATIONS_FILE ?= ../dat/stations.dat

INIT ?=
METHOD ?= nn
TABLE_FORMAT ?= tex
TABLE_OUTPUT ?=
L2SEG_UNIT ?= seconds
INIT_TARGET_RAW := $(or $(INIT),$(init),noport)
INIT_TARGET := $(if $(filter gi,$(INIT_TARGET_RAW)),ge,$(INIT_TARGET_RAW))
SWEEP_INIT_RAW := $(or $(INIT),$(init),noport)
SWEEP_INIT := $(if $(filter gi,$(SWEEP_INIT_RAW)),ge,$(SWEEP_INIT_RAW))
VALID_INIT_TYPES := nn ge ci noport fixedport

.PHONY: all clean config build prepare-routing-data preprocessing survey export_survey init noport noport-opt sweep sweep_table plot plot-routes plot-survey-routes plot-sweep-summary help

all: build

$(BUILD_DIR)/CMakeCache.txt:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release ../src

config: $(BUILD_DIR)/CMakeCache.txt

build: $(BUILD_DIR)/CMakeCache.txt
	@cd $(BUILD_DIR) && $(MAKE)

INIT_TARGET_JSON = ../sol/$(INIT_TARGET)/init.json

init:
ifeq ($(filter $(INIT_TARGET),$(VALID_INIT_TYPES)),)
	@echo "Unsupported init type: $(INIT_TARGET_RAW)"
	@echo "Use INIT=nn|ge|ci|noport|fixedport (or init=nn|ge|ci|noport|fixedport)."
	@exit 1
else
	@:
endif

init: $(INIT_TARGET_JSON)

prepare-routing-data: $(COUNTRY_DB)

$(COUNTRY_DB): $(COUNTRY_ISLAND) $(COUNTRY_DAT) $(COUNTRY_PORT_FILE) $(COUNTRY_BOAT_FILE) $(COUNTRY_STATIONS_FILE) | $(BUILD_DIR)/CMakeCache.txt
	@mkdir -p $(dir $@)
	@cd $(BUILD_DIR) && $(MAKE) gsp_prepare_routing_data
	@rm -f $@.tmp
	@cd $(BUILD_DIR) && ./common/gsp_prepare_routing_data \
		--db $@.tmp \
		--coastline-file $(COUNTRY_ISLAND) \
		--waypoint-file $(COUNTRY_DAT) \
		--port-file $(COUNTRY_PORT_FILE) \
		--boat-file $(COUNTRY_BOAT_FILE) \
		--stations-file $(COUNTRY_STATIONS_FILE)
	@mv $@.tmp $@

preprocessing: prepare-routing-data survey

survey: build
	@cd $(BUILD_DIR) && $(MAKE) survey

noport noport-opt: ../sol/noport/noport.json

../sol/noport/noport.json: $(COUNTRY_DB)
	@mkdir -p ../sol/noport
	@bash -lc 'cd "$(BUILD_DIR)" && set -o pipefail && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_noport_opt --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --output "../sol/noport/noport.json" 2>&1 | tee -a ../sol/noport/noport.log'

../sol/noport/init.json: ../sol/noport/noport.json
	@mkdir -p ../sol/noport
	@echo "Generating init from ordered input -> $@"
	@bash -lc 'cd "$(BUILD_DIR)" && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_init_from_order --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "../sol/noport/noport.json" --output "../sol/noport/init.json"'

define init_rule
../sol/$(1)/init.json: $(COUNTRY_DB)
	@mkdir -p ../sol/$(1)
	@echo "Generating $(2) init -> $$@"
	@bash -lc 'cd "$(BUILD_DIR)" && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_gurobi --mode init --strategy $(1) --database "$(COUNTRY_DB)" --output "../sol/$(1)/init.json" --config "$(CONFIG_YAML)"'
endef

define sweep_config_rule
../sol/$(1)/sweep.json: ../sol/$(1)/init.json
	@mkdir -p ../sol/$(1)
	@echo "Running $(2) sweep -> $$@"
	@bash -lc 'cd "$(BUILD_DIR)" && set -o pipefail && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_gurobi --mode sweep --strategy $(1) --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "../sol/$(1)/init.json" --output "$$@" 2>&1 | tee "../sol/$(1)/sweep.log"'
endef

$(eval $(call init_rule,nn,NN))
$(eval $(call init_rule,ge,GE))
$(eval $(call init_rule,ci,CI))

$(eval $(call sweep_config_rule,nn,NN))
$(eval $(call sweep_config_rule,ge,GE))
$(eval $(call sweep_config_rule,ci,CI))
$(eval $(call sweep_config_rule,noport,noport))

SWEEP_TARGET = ../sol/$(SWEEP_INIT)/sweep.json


sweep:
ifeq ($(filter $(SWEEP_INIT),$(VALID_INIT_TYPES)),)
	@echo "Unsupported init type: $(SWEEP_INIT_RAW)"
	@echo "Use INIT=nn|ge|ci|noport|fixedport (or init=nn|ge|ci|noport|fixedport)."
	@exit 1
else
	@:
endif

sweep: $(SWEEP_TARGET)

sweep_table:
	@bash -lc 'set -euo pipefail; cd ..; \
		args=(R/make_sweep_table.R --input-dir "sol/$(METHOD)" --format "$(TABLE_FORMAT)" --l2seg-unit "$(L2SEG_UNIT)" --warn-missing); \
		if [ -n "$(TABLE_OUTPUT)" ]; then \
			args+=(--output "$(TABLE_OUTPUT)"); \
		fi; \
		Rscript "$${args[@]}"'

plot-routes: $(ROUTE_PNGS)

../sol/%.png: ../sol/%.json
	@cd .. && Rscript R/plot_survey_route.R "$(patsubst ../%,%,$<)" "$(patsubst ../%,%,$@)"

../dat/coastline_waypoints_ports.png:
	@bash -lc 'cd .. && Rscript R/plot_country.R dat/gsp.db dat/coastline_waypoints_ports.png'

../sol/survey_overview.png:
	@bash -lc 'cd .. && Rscript R/plot_survey_overview.R dat/gsp.db sol/survey_overview.png'

../sol/survey/routes_combined.png:
	@bash -lc 'cd .. && Rscript R/plot_survey_routes_combined.R "sol/survey/boat*.json" "sol/survey/routes_combined.png" "dat/gsp.db"'

../sol/sweep_summary_panels.png:
	@bash -lc 'cd .. && Rscript R/plot_sweep_summary_panels.R sol sol/sweep_summary_panels.png'

plot: ../dat/coastline_waypoints_ports.png ../sol/survey_overview.png plot-routes

plot-survey-routes: ../sol/survey/routes_combined.png

plot-sweep-summary: ../sol/sweep_summary_panels.png

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
	@echo "Run from repo root with: make -C src <target> ..."
	@echo ""
	@echo "Data Preparation:"
	@echo "  make -C src prepare-routing-data"
	@echo ""
	@echo "Phase 0: Initilization"
	@echo "  make -C src init              # defaults to INIT=noport"
	@echo "  make -C src init INIT=nn"
	@echo "  make -C src init INIT=ge"
	@echo "  make -C src init INIT=ci"
	@echo "  make -C src init INIT=noport"
	@echo "  make -C src noport"
	@echo ""
	@echo "Phase 1: Sweep"
	@echo "  make -C src sweep INIT=nn      # run the single L2SEG value from config"
	@echo "  make -C src sweep init=noport  # lowercase variable also works"
	@echo "  make -C src sweep INIT=ge"
	@echo "  make -C src sweep_table METHOD=nn TABLE_FORMAT=tex"
	@echo "  make -C src sweep_table METHOD=nn TABLE_FORMAT=tex TABLE_OUTPUT=../Paper/tables/tbl_mh_init_ci.tex"
	@echo "  make -C src sweep_table METHOD=nn TABLE_FORMAT=md L2SEG_UNIT=minutes"
	@echo ""
	@echo "Other targets:"
	@echo "  make -C src build"
	@echo "  make -C src survey"
	@echo "  make -C src plot"
	@echo "  make -C src plot-routes"
	@echo "  make -C src plot-survey-routes"
	@echo "  make -C src plot-sweep-summary"
	@echo "  make -C src clean"
	@echo "  make -C src reconfigure"
