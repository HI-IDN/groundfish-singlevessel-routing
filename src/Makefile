# Lightweight Makefile - CMake wrapper
# Build directory is at ../build (root level)

BUILD_DIR = ../build
CONFIG_YAML = ../config/gsp_solver.yaml

# construction.json / segment.json → single-vessel route PNGs + .md summaries
ROUTE_JSONS = $(shell find ../sol \( -name "construction.json" -o -name "segment.json" \) ! -path "*/survey/*")
ROUTE_PNGS  = $(patsubst %.json,%.png,$(ROUTE_JSONS))

# refinement*.json → route PNGs + .md summaries (separate so we can depend on them)
REFINEMENT_ROUTE_JSONS = $(shell find ../sol -name "refinement*.json")
REFINEMENT_ROUTE_PNGS  = $(patsubst %.json,%.png,$(REFINEMENT_ROUTE_JSONS))

# Alias used for trajectory / MIP-runtime plot dependencies
REFINEMENT_JSONS = $(REFINEMENT_ROUTE_JSONS)

COUNTRY_DB ?= ../dat/gsp.db
COUNTRY_ISLAND ?= ../dat/island.tsv
COUNTRY_DAT ?= ../dat/waypoints.dat
COUNTRY_PORT_FILE ?= ../dat/ports.dat
COUNTRY_BOAT_FILE ?= ../dat/boats.dat
COUNTRY_STATIONS_FILE ?= ../dat/stations.dat

METHOD ?=
TABLE_FORMAT ?= tex
TABLE_OUTPUT ?=
L2SEG_UNIT ?= seconds
L2SEG ?=
L2SEG_VARIANTS ?= $(shell bash -lc "sed -n -E 's/^[[:space:]]*l2seg_variants:[[:space:]]*\\[([^]]*)\\].*/\\1/p' '$(CONFIG_YAML)' | tr ',' ' '")
L2SEG_DEFAULT_VALUE ?= $(shell bash -lc "sed -n -E 's/^[[:space:]]*2seg:[[:space:]]*([0-9]+).*/\\1/p' '$(CONFIG_YAML)' | head -n 1")
METHOD_RAW := $(or $(METHOD),nn)
METHOD_NAME := $(if $(filter gi,$(METHOD_RAW)),ge,$(METHOD_RAW))
VALID_METHODS := nn ge ci noport fixedport
VALID_REFINEMENT_METHODS := nn ge ci noport
EXPERIMENT_REFINEMENT_METHODS := noport nn ge ci
FIXEDPORT_CANDIDATE_JSON = ../sol/fixedport/candidate_ports.json
SURVEY_DAT = ../dat/survey2023spring.dat
SURVEY_MULTIVESSEL_JSON = ../sol/survey/multivessel.json
SURVEY_BOAT_JSONS = $(SURVEY_MULTIVESSEL_JSON)

.PHONY: all clean config build prepare-routing-data preprocessing survey export_survey fixedport_candidates fixedport construction segment refinement refinement_l2seg noport sweep_table plot plot-routes plot-refinement plot-mip-runtime plot-survey-routes plot-sweep-summary experiments help

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

refinement_l2seg: $(SEGMENT_TARGET_JSON)
	@mkdir -p ../sol/$(METHOD_NAME)
	@bash -lc 'set -euo pipefail; \
		case " $(VALID_REFINEMENT_METHODS) " in *" $(METHOD_NAME) "*) ;; *) echo "Unsupported refinement method: $(METHOD_RAW)"; echo "Use METHOD=nn|ge|ci|noport."; exit 1;; esac; \
		if [ -z "$(strip $(L2SEG))" ]; then echo "Missing L2SEG value."; echo "Use L2SEG=$(L2SEG_VARIANTS) $(L2SEG_DEFAULT_VALUE)."; exit 1; fi; \
		limit="$(strip $(L2SEG))"; \
		suffix=""; \
		if [ "$$limit" != "0" ]; then suffix="_$$limit"; fi; \
		out="../sol/$(METHOD_NAME)/refinement$${suffix}.json"; \
		log="../sol/$(METHOD_NAME)/refinement$${suffix}.log"; \
		if [ -f "$$out" ]; then \
			echo "Already complete (file exists): $$out"; \
			echo "  To force re-run: rm $$out && make -C src refinement_l2seg METHOD=$(METHOD_NAME) L2SEG=$$limit"; \
			exit 0; \
		fi; \
		echo "Running $(METHOD_NAME) refinement L2SEG=$${limit}s -> $$out"; \
		cd "$(BUILD_DIR)"; \
		PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_gurobi --mode refinement --strategy $(METHOD_NAME) --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "../sol/$(METHOD_NAME)/segment.json" --output "$$out" --l2seg-limit "$$limit" 2>&1 | tee "$$log"'

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

../sol/survey/multivessel.json: $(SURVEY_DAT) $(COUNTRY_DB) $(CONFIG_YAML) $(BUILD_DIR)/CMakeCache.txt
	@mkdir -p ../sol/survey
	@echo "Generating multivessel survey JSON -> $@"
	@cd $(BUILD_DIR) && $(MAKE) historical_survey
	@bash -lc 'cd "$(BUILD_DIR)" && ./historical_survey "$(abspath $(SURVEY_DAT))" "$(abspath $(COUNTRY_DB))" "$(abspath ../sol/survey/multivessel.json)" "$(abspath $(CONFIG_YAML))"'

$(FIXEDPORT_CANDIDATE_JSON): $(SURVEY_MULTIVESSEL_JSON) $(BUILD_DIR)/CMakeCache.txt
	@mkdir -p ../sol/fixedport
	@cd $(BUILD_DIR) && $(MAKE) survey_fixedport_candidates
	@bash -lc 'cd "$(BUILD_DIR)" && ./survey_fixedport_candidates "$(abspath $(SURVEY_MULTIVESSEL_JSON))" "$(abspath $(FIXEDPORT_CANDIDATE_JSON))"'

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
	@bash -lc 'cd "$(BUILD_DIR)" && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_segment_from_construction --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "../sol/noport/construction.json" --output "../sol/noport/segment.json"'

define construction_rule
../sol/$(1)/construction.json: $(COUNTRY_DB)
	@mkdir -p ../sol/$(1)
	@echo "Generating $(2) construction -> $$@"
	@bash -lc 'cd "$(BUILD_DIR)" && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_gurobi --mode construction --strategy $(1) --database "$(COUNTRY_DB)" --output "../sol/$(1)/construction.json" --config "$(CONFIG_YAML)"'
endef

define segment_from_construction_rule
../sol/$(1)/segment.json: ../sol/$(1)/construction.json
	@mkdir -p ../sol/$(1)
	@echo "Generating $(2) segment from construction -> $$@"
	@bash -lc 'cd "$(BUILD_DIR)" && PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_segment_from_construction --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "../sol/$(1)/construction.json" --output "../sol/$(1)/segment.json" --strategy $(1)'
endef

define refinement_rule
../sol/$(1)/refinement.json: ../sol/$(1)/segment.json
	@mkdir -p ../sol/$(1)
	@echo "Running $(2) refinement -> $$@"
	@bash -lc 'set -euo pipefail; \
		cd "$(BUILD_DIR)"; \
		PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_gurobi --mode refinement --strategy $(1) --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "../sol/$(1)/segment.json" --output "$$@" 2>&1 | tee "../sol/$(1)/refinement.log"'
endef

$(eval $(call construction_rule,nn,NN))
$(eval $(call construction_rule,ge,GE))
$(eval $(call construction_rule,ci,CI))

$(eval $(call segment_from_construction_rule,nn,NN))
$(eval $(call segment_from_construction_rule,ge,GE))
$(eval $(call segment_from_construction_rule,ci,CI))
$(eval $(call segment_from_construction_rule,fixedport,fixedport))

$(eval $(call refinement_rule,nn,NN))
$(eval $(call refinement_rule,ge,GE))
$(eval $(call refinement_rule,ci,CI))
$(eval $(call refinement_rule,noport,noport))

# ---------------------------------------------------------------------------
# Per-L2SEG refinement file rules (one real file target per method × l2seg)
# Make checks timestamps so completed runs are skipped automatically.
# ---------------------------------------------------------------------------
define refinement_l2seg_rule
../sol/$(1)/refinement_$(2).json: ../sol/$(1)/segment.json $(CONFIG_YAML)
	@mkdir -p ../sol/$(1)
	@echo "Running $(1) refinement L2SEG=$(2)s -> $$@"
	@bash -lc 'set -euo pipefail; \
		cd "$(BUILD_DIR)"; \
		PATH="$$GUROBI_HOME/bin:$$PATH" ./gsp_gurobi --mode refinement --strategy $(1) --database "$(COUNTRY_DB)" --config "$(CONFIG_YAML)" --input "../sol/$(1)/segment.json" --output "../sol/$(1)/refinement_$(2).json" --l2seg-limit $(2) 2>&1 | tee "../sol/$(1)/refinement_$(2).log"'
endef

$(foreach m,$(EXPERIMENT_REFINEMENT_METHODS),\
  $(foreach l,$(L2SEG_VARIANTS),\
    $(eval $(call refinement_l2seg_rule,$(m),$(l)))))

# All l2seg file targets (used by experiments)
EXPERIMENT_L2SEG_TARGETS = $(foreach m,$(EXPERIMENT_REFINEMENT_METHODS),\
  $(foreach l,$(L2SEG_VARIANTS),\
    ../sol/$(m)/refinement_$(l).json))

# Default (uncapped) targets
EXPERIMENT_DEFAULT_TARGETS = $(foreach m,$(EXPERIMENT_REFINEMENT_METHODS),\
  ../sol/$(m)/refinement.json)

sweep_table:
	@bash -lc 'set -euo pipefail; cd ..; \
		args=(R/make_sweep_table.R --input-dir "sol/$(METHOD_NAME)" --format "$(TABLE_FORMAT)" --l2seg-unit "$(L2SEG_UNIT)" --warn-missing); \
		if [ -n "$(TABLE_OUTPUT)" ]; then \
			args+=(--output "$(TABLE_OUTPUT)"); \
		fi; \
		Rscript "$${args[@]}"'

plot-routes: $(ROUTE_PNGS) $(REFINEMENT_ROUTE_PNGS)

../sol/%.png: ../sol/%.json
	@cd .. && Rscript R/plot_single_vessel_route.R "$(patsubst ../%,%,$<)" "$(patsubst ../%,%,$@)"

../dat/coastline_waypoints_ports.png:
	@bash -lc 'cd .. && Rscript R/plot_country.R dat/gsp.db dat/coastline_waypoints_ports.png'

../dat/survey_overview.png:
	@bash -lc 'cd .. && Rscript R/plot_survey_overview.R dat/gsp.db dat/survey_overview.png'

../sol/survey/multivessel.png:
	@bash -lc 'cd .. && Rscript R/plot_multi_vessel_route.R "sol/survey/multivessel.json" "sol/survey/multivessel.png" "dat/gsp.db"'

../sol/sweep_summary_panels.png:
	@bash -lc 'cd .. && Rscript R/plot_sweep_summary_panels.R sol sol/sweep_summary_panels.png'

# Refinement sweep trajectory plots (make_refinement_table.R writes multiple PNGs;
# use refinement_transit_sweeps.png as the representative stamp target).
../sol/refinement_transit_sweeps.png: $(REFINEMENT_JSONS) $(CONFIG_YAML)
	@bash -lc 'cd .. && Rscript R/make_refinement_table.R'

plot-refinement: ../sol/refinement_transit_sweeps.png

# MIP runtime plot
../sol/mip_runtime_vs_size.png: $(REFINEMENT_JSONS) \
    $(shell find ../sol \( -name "construction.json" -o -name "segment.json" \) ! -path "*/survey/*")
	@bash -lc 'cd .. && Rscript R/plot_mip_runtime.R'

plot-mip-runtime: ../sol/mip_runtime_vs_size.png

plot: ../dat/coastline_waypoints_ports.png ../dat/survey_overview.png \
      ../sol/survey/multivessel.png \
      plot-routes plot-refinement plot-mip-runtime

experiments: prepare-routing-data $(EXPERIMENT_L2SEG_TARGETS) $(EXPERIMENT_DEFAULT_TARGETS)
	@echo "All experiments up to date."

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
	@echo "  make -C src refinement_l2seg METHOD=nn L2SEG=60"
	@echo ""
	@echo "Shortcuts"
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
	@echo "  make -C src experiments          # runs L2SEG values from config/gsp_solver.yaml in order"
	@echo "  make -C src plot                  # all plots"
	@echo "  make -C src plot-routes           # construction/segment/refinement route PNGs"
	@echo "  make -C src plot-refinement       # sweep trajectory PNGs (make_refinement_table.R)"
	@echo "  make -C src plot-mip-runtime      # MIP runtime plot (plot_mip_runtime.R)"
	@echo "  make -C src plot-survey-routes"
	@echo "  make -C src plot-sweep-summary"
	@echo "  make -C src clean"
	@echo "  make -C src reconfigure"
