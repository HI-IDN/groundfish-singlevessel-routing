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
METHOD ?=
TABLE_FORMAT ?= tex
TABLE_OUTPUT ?=
L2SEG_UNIT ?= seconds
METHOD_RAW := $(or $(METHOD),$(INIT),$(init),nn)
METHOD_NAME := $(if $(filter gi,$(METHOD_RAW)),ge,$(METHOD_RAW))
VALID_METHODS := nn ge ci noport fixedport
VALID_REFINEMENT_METHODS := nn ge ci noport
EXPERIMENT_REFINEMENT_METHODS := noport nn ge ci
FIXEDPORT_CANDIDATE_JSON = ../dat/candidate_ports.json
SURVEY_DAT = ../dat/survey2023spring.dat
SURVEY_BOAT_JSONS = ../sol/survey/boat1.json ../sol/survey/boat2.json ../sol/survey/boat3.json ../sol/survey/boat4.json

.PHONY: all clean config build prepare-routing-data preprocessing survey export_survey fixedport_candidates fixedport construction segment refinement init noport sweep sweep_table plot plot-routes plot-survey-routes plot-sweep-summary experiments help

all: build

$(BUILD_DIR)/CMakeCache.txt:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release ../src

config: $(BUILD_DIR)/CMakeCache.txt

build: $(BUILD_DIR)/CMakeCache.txt
	@cd $(BUILD_DIR) && $(MAKE)

CONSTRUCTION_TARGET_JSON = ../sol/$(METHOD_NAME)/construction.json
SEGMENT_TARGET_JSON = ../sol/$(METHOD_NAME)/segment.json
REFINEMENT_TARGET_JSON = ../sol/$(METHOD_NAME)/refinement.json

construction:
ifeq ($(filter $(METHOD_NAME),$(VALID_METHODS)),)
	@echo "Unsupported method: $(METHOD_RAW)"
	@echo "Use METHOD=nn|ge|ci|noport|fixedport."
	@exit 1
else
	@:
endif

construction: $(CONSTRUCTION_TARGET_JSON)

segment:
ifeq ($(filter $(METHOD_NAME),$(VALID_METHODS)),)
	@echo "Unsupported method: $(METHOD_RAW)"
	@echo "Use METHOD=nn|ge|ci|noport|fixedport."
	@exit 1
else
	@:
endif

segment: $(SEGMENT_TARGET_JSON)

refinement:
ifeq ($(filter $(METHOD_NAME),$(VALID_REFINEMENT_METHODS)),)
	@echo "Unsupported refinement method: $(METHOD_RAW)"
	@echo "Use METHOD=nn|ge|ci|noport."
	@exit 1
else
	@:
endif

refinement: $(REFINEMENT_TARGET_JSON)

init: segment

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

survey: $(BUILD_DIR)/CMakeCache.txt
	@cd $(BUILD_DIR) && $(MAKE) survey

../sol/survey/boat%.json: $(SURVEY_DAT) $(COUNTRY_DB) $(BUILD_DIR)/CMakeCache.txt
	@mkdir -p ../sol/survey
	@cd $(BUILD_DIR) && $(MAKE) historical_survey
	@bash -lc 'cd "$(BUILD_DIR)/common" && ../historical_survey "$(SURVEY_DAT)" "$(COUNTRY_DB)" "../sol/survey" "$*"'

$(FIXEDPORT_CANDIDATE_JSON): $(SURVEY_BOAT_JSONS) $(CONFIG_YAML) $(BUILD_DIR)/CMakeCache.txt
	@mkdir -p ../dat
	@cd $(BUILD_DIR) && $(MAKE) survey_fixedport_candidates
	@bash -lc 'cd "$(BUILD_DIR)" && ./survey_fixedport_candidates "$(CONFIG_YAML)" "$(COUNTRY_DB)" "$(FIXEDPORT_CANDIDATE_JSON)" "../sol/survey/boat1.json" "../sol/survey/boat2.json" "../sol/survey/boat3.json" "../sol/survey/boat4.json"'

fixedport_candidates: $(FIXEDPORT_CANDIDATE_JSON)

fixedport: ../sol/fixedport/construction.json

../sol/fixedport/construction.json: $(COUNTRY_DB) $(FIXEDPORT_CANDIDATE_JSON)
	@mkdir -p ../sol/fixedport
	@bash -lc 'cd "$(BUILD_DIR)" && set -o pipefail && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_fixedport --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --candidates "$(FIXEDPORT_CANDIDATE_JSON)" --output "../sol/fixedport/construction.json" 2>&1 | tee -a ../sol/fixedport/construction.log'

noport: ../sol/noport/construction.json

../sol/noport/construction.json: $(COUNTRY_DB)
	@mkdir -p ../sol/noport
	@bash -lc 'cd "$(BUILD_DIR)" && set -o pipefail && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_noport --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --output "../sol/noport/construction.json" 2>&1 | tee -a ../sol/noport/construction.log'

../sol/noport/segment.json: ../sol/noport/construction.json
	@mkdir -p ../sol/noport
	@echo "Generating segment from ordered input -> $@"
	@bash -lc 'cd "$(BUILD_DIR)" && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_init_from_order --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "../sol/noport/construction.json" --output "../sol/noport/segment.json"'

define construction_rule
../sol/$(1)/construction.json: $(COUNTRY_DB)
	@mkdir -p ../sol/$(1)
	@echo "Generating $(2) construction -> $$@"
	@bash -lc 'cd "$(BUILD_DIR)" && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_gurobi --mode init --strategy $(1) --database "$(COUNTRY_DB)" --output "../sol/$(1)/construction.json" --config "$(CONFIG_YAML)"'
endef

define copy_construction_to_segment_rule
../sol/$(1)/segment.json: ../sol/$(1)/construction.json
	@mkdir -p ../sol/$(1)
	@echo "Promoting $(2) construction to segmented baseline -> $$@"
	@cp "../sol/$(1)/construction.json" "../sol/$(1)/segment.json"
endef

define refinement_rule
../sol/$(1)/refinement.json: ../sol/$(1)/segment.json
	@mkdir -p ../sol/$(1)
	@echo "Running $(2) refinement -> $$@"
	@bash -lc 'cd "$(BUILD_DIR)" && set -o pipefail && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_gurobi --mode sweep --strategy $(1) --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "../sol/$(1)/segment.json" --output "$$@" 2>&1 | tee "../sol/$(1)/refinement.log"'
endef

$(eval $(call construction_rule,nn,NN))
$(eval $(call construction_rule,ge,GE))
$(eval $(call construction_rule,ci,CI))

$(eval $(call copy_construction_to_segment_rule,nn,NN))
$(eval $(call copy_construction_to_segment_rule,ge,GE))
$(eval $(call copy_construction_to_segment_rule,ci,CI))
$(eval $(call copy_construction_to_segment_rule,fixedport,fixedport))

$(eval $(call refinement_rule,nn,NN))
$(eval $(call refinement_rule,ge,GE))
$(eval $(call refinement_rule,ci,CI))
$(eval $(call refinement_rule,noport,noport))

sweep: refinement

sweep_table:
	@bash -lc 'set -euo pipefail; cd ..; \
		args=(R/make_sweep_table.R --input-dir "sol/$(METHOD_NAME)" --format "$(TABLE_FORMAT)" --l2seg-unit "$(L2SEG_UNIT)" --warn-missing); \
		if [ -n "$(TABLE_OUTPUT)" ]; then \
			args+=(--output "$(TABLE_OUTPUT)"); \
		fi; \
		Rscript "$${args[@]}"'

plot-routes: $(ROUTE_PNGS)

../sol/%.png: ../sol/%.json
	@cd .. && Rscript R/plot_single_vessel_route.R "$(patsubst ../%,%,$<)" "$(patsubst ../%,%,$@)"

../dat/coastline_waypoints_ports.png:
	@bash -lc 'cd .. && Rscript R/plot_country.R dat/gsp.db dat/coastline_waypoints_ports.png'

../dat/survey_overview.png:
	@bash -lc 'cd .. && Rscript R/plot_survey_overview.R dat/gsp.db dat/survey_overview.png'

../sol/survey/multivessel.png:
	@bash -lc 'cd .. && Rscript R/plot_multi_vessel_route.R "sol/survey/boat*.json" "sol/survey/multivessel.png" "dat/gsp.db"'

../sol/sweep_summary_panels.png:
	@bash -lc 'cd .. && Rscript R/plot_sweep_summary_panels.R sol sol/sweep_summary_panels.png'

plot: ../dat/coastline_waypoints_ports.png ../dat/survey_overview.png ../sol/survey/multivessel.png plot-routes

EXPERIMENT_TARGETS = \
	../sol/noport/construction.json \
	../sol/noport/segment.json \
	$(foreach method,$(EXPERIMENT_REFINEMENT_METHODS),../sol/$(method)/refinement.json)

experiments: prepare-routing-data $(EXPERIMENT_TARGETS)

plot-survey-routes: ../sol/survey/multivessel.png

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
	@echo "Construction / Segment / Refinement"
	@echo "  make -C src construction METHOD=nn"
	@echo "  make -C src construction METHOD=noport"
	@echo "  make -C src segment METHOD=nn"
	@echo "  make -C src segment METHOD=noport"
	@echo "  make -C src refinement METHOD=nn"
	@echo "  make -C src refinement METHOD=noport"
	@echo ""
	@echo "Compatibility aliases"
	@echo "  make -C src init INIT=nn"
	@echo "  make -C src sweep INIT=nn"
	@echo "  make -C src noport             # same as construction METHOD=noport"
	@echo "  make -C src fixedport          # same as construction METHOD=fixedport"
	@echo ""
	@echo "Tables"
	@echo "  make -C src sweep_table METHOD=nn TABLE_FORMAT=tex"
	@echo "  make -C src sweep_table METHOD=nn TABLE_FORMAT=tex TABLE_OUTPUT=../Paper/tables/tbl_mh_init_ci.tex"
	@echo "  make -C src sweep_table METHOD=nn TABLE_FORMAT=md L2SEG_UNIT=minutes"
	@echo ""
	@echo "Other targets:"
	@echo "  make -C src build"
	@echo "  make -C src survey"
	@echo "  make -C src fixedport_candidates"
	@echo "  make -C src fixedport"
	@echo "  make -C src experiments"
	@echo "  make -C src plot"
	@echo "  make -C src plot-routes"
	@echo "  make -C src plot-survey-routes"
	@echo "  make -C src plot-sweep-summary"
	@echo "  make -C src clean"
	@echo "  make -C src reconfigure"
