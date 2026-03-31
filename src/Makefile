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

L2SEG ?=
INIT ?=
METHOD ?= nn
TABLE_FORMAT ?= tex
TABLE_OUTPUT ?=
L2SEG_UNIT ?= seconds
SWEEP_INIT_RAW := $(or $(INIT),$(init),nn)
SWEEP_INIT := $(if $(filter gi,$(SWEEP_INIT_RAW)),ge,$(SWEEP_INIT_RAW))
VALID_INIT_TYPES := nn ge ci opt

.PHONY: all clean config build prepare-routing-data preprocessing survey export_survey init init_nn init_ge init_gi init_ci init_opt noport-opt sweep sweep_table plot plot-routes help

all: build

config:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release ../src

build: config
	@cd $(BUILD_DIR) && $(MAKE)

init: init_nn init_ge init_ci init_opt

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

init_nn: ../sol/nn/init.json

init_ge: ../sol/ge/init.json

init_gi: init_ge

init_ci: ../sol/ci/init.json

noport-opt: ../sol/opt/noport.json

init_opt: ../sol/opt/init.json

../sol/opt/noport.json: $(COUNTRY_DB)
	@mkdir -p ../sol/opt
	@bash -lc 'cd "$(BUILD_DIR)" && set -o pipefail && PATH="$$GUROBI_HOME/bin:$$PATH" $(MAKE) run_noport_opt 2>&1 | tee -a ../sol/opt/noport.log'

../sol/opt/init.json: ../sol/opt/noport.json
	@mkdir -p ../sol/opt
	@echo "Generating OPT init -> $@"
	@cd $(BUILD_DIR) && $(MAKE) run_init_opt

define init_rule
../sol/$(1)/init.json: $(COUNTRY_DB)
	@mkdir -p ../sol/$(1)
	@echo "Generating $(2) init -> $$@"
	@cd $(BUILD_DIR) && $(MAKE) run_init_$(1)
endef

define sweep_rule
../sol/$(1)/sweep_%.json: ../sol/$(1)/init.json
	@mkdir -p ../sol/$(1)
	@echo "Running $(2) sweep for L2SEG=$$* -> $$@"
	@bash -lc 'cd "$(BUILD_DIR)" && set -o pipefail && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_gurobi --mode sweep --strategy $(1) --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "../sol/$(1)/init.json" --output "$$@" --l2seg "$$*" 2>&1 | tee "../sol/$(1)/sweep_$$*.log"'
endef

$(eval $(call init_rule,nn,NN))
$(eval $(call init_rule,ge,GE))
$(eval $(call init_rule,ci,CI))

$(eval $(call sweep_rule,nn,NN))
$(eval $(call sweep_rule,ge,GE))
$(eval $(call sweep_rule,ci,CI))
$(eval $(call sweep_rule,opt,OPT))

define run_sweep_template
	@bash -lc 'set -euo pipefail; \
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
		for value in $$run_values; do \
			value=$$(echo "$$value" | xargs); \
			$(MAKE) "../sol/$(1)/sweep_$${value}.json"; \
		done'
endef

sweep:
ifeq ($(filter $(SWEEP_INIT),$(VALID_INIT_TYPES)),)
	@echo "Unsupported init type: $(SWEEP_INIT_RAW)"
	@echo "Use INIT=nn|ge|ci|opt (or init=nn|ge|ci|opt)."
	@exit 1
else
	$(call run_sweep_template,$(SWEEP_INIT))
endif

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
	@bash -lc 'cd .. && Rscript R/plot_survey_overview.R'

plot: ../dat/coastline_waypoints_ports.png ../sol/survey_overview.png plot-routes

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
	@echo "  make -C src init_nn"
	@echo "  make -C src init_gi      # alias for init_ge"
	@echo "  make -C src init_ci"
	@echo "  make -C src noport-opt"
	@echo "  make -C src init_opt"
	@echo ""
	@echo "Phase 1: Sweep"
	@echo "  make -C src sweep INIT=nn      # run all L2SEG values from config"
	@echo "  make -C src sweep init=opt     # lowercase variable also works"
	@echo "  make -C src sweep INIT=gi      # gi maps to ge"
	@echo "  make -C src sweep INIT=ci L2SEG=120"
	@echo "  make -C src sweep_table METHOD=nn TABLE_FORMAT=tex"
	@echo "  make -C src sweep_table METHOD=nn TABLE_FORMAT=tex TABLE_OUTPUT=../Paper/tables/tbl_mh_init_ci.tex"
	@echo "  make -C src sweep_table METHOD=nn TABLE_FORMAT=md L2SEG_UNIT=minutes"
	@echo ""
	@echo "Other targets:"
	@echo "  make -C src build"
	@echo "  make -C src survey"
	@echo "  make -C src plot"
	@echo "  make -C src plot-routes"
	@echo "  make -C src clean"
	@echo "  make -C src reconfigure"
