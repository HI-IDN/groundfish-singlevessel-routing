# Lightweight Makefile - CMake wrapper
# Build directory is at ../build (root level)

BUILD_DIR = ../build

COUNTRY_DB ?= ../dat/gsp_data.db
COUNTRY_ISLAND ?= ../dat/island.bin
COUNTRY_DAT ?= ../dat/waypoints.dat
COUNTRY_PORT_FILE ?= ../dat/ports.dat
COUNTRY_BOAT_FILE ?= ../dat/boats.dat

.PHONY: all clean config build country stations distance stations-with-distance preprocessing survey export_survey init init_nn init_ge init_ci init_opt help

all: build

config:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release ../src-refactor

build: config
	@cd $(BUILD_DIR) && $(MAKE)

init: build

country: config
	@cd $(BUILD_DIR) && $(MAKE) gsp_country
	@cd $(BUILD_DIR) && ./gsp_country \
		--db $(COUNTRY_DB) \
		--coastline-file $(COUNTRY_ISLAND) \
		--waypoint-file $(COUNTRY_DAT) \
		--seed-hints-only \
		--port-file $(COUNTRY_PORT_FILE) \
		--boat-file $(COUNTRY_BOAT_FILE)
	@echo ""
	@echo "Plot command:"
	@echo "  Rscript R/plot_country.R dat/gsp_data.db"

stations: config
	@cd $(BUILD_DIR) && $(MAKE) stations

distance: config
	@cd $(BUILD_DIR) && $(MAKE) distance

preprocessing: country stations distance survey

survey: build
	@echo "[Exporting historical survey to JSON]"
	@cd $(BUILD_DIR) && ./historical_survey ../dat/gsp_data.db ../dat/survey2023spring.dat
	@echo "Historical survey exported to sol/survey/boat*.json"

init_nn: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_nn

init_ge: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_ge

init_ci: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_ci

init_opt: build
	@cd $(BUILD_DIR) && $(MAKE) run_init_opt

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
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
	@echo "Preprocessing Workflow:"
	@echo "  make country"
	@echo "  make stations"
	@echo "  make distance"
	@echo "  make historical-survey"
	@echo ""
	@echo "Other targets:"
	@echo "  make build"
	@echo "  make init_nn"
	@echo "  make init_ge"
	@echo "  make init_ci"
	@echo "  make init_opt"
	@echo "  make clean"
	@echo "  make reconfigure"
