# Root Makefile for src-refactor

.PHONY: all clean prep-data help

all:
	@echo "Building src-refactor modules..."
	$(MAKE) -C common all
	@echo "✓ Build complete"

clean:
	@echo "Cleaning src-refactor modules..."
	$(MAKE) -C common clean
	$(MAKE) -C preprocess clean
	$(MAKE) -C init clean
	$(MAKE) -C mip clean
	$(MAKE) -C sweep clean
	@echo "✓ Clean complete"

# Prepare data - parse .dat file (all boats) and display summary
prep-data:
	@echo "=== Data Preparation ==="
	@echo "Checking for libutils..."
	@if [ ! -f ../lib/libutils.so ] && [ ! -f ../lib/libutils.dylib ] && [ ! -f ../lib/utils.dll ] && [ ! -f ../bin/utils.dll ]; then \
		echo "libutils not found. Building from src/..."; \
		$(MAKE) -C ../src utils; \
	else \
		echo "✓ libutils found"; \
	fi
	@echo ""
	$(MAKE) -C common prep

# Help target
help:
	@echo "Available targets:"
	@echo "  make all             - Build all modules"
	@echo "  make clean           - Clean all build artifacts"
	@echo "  make prep-data       - Parse .dat file and prepare data (all boats)"
	@echo "  make help            - Show this help message"



