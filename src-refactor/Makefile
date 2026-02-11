# Root Makefile for src-refactor

.PHONY: all clean preprocessing help

all:
	@echo "Building src-refactor modules..."
	$(MAKE) -C common all
	@echo "✓ Build complete"

clean:
	@echo "Cleaning src-refactor modules..."
	$(MAKE) -C common clean
	$(MAKE) -C init clean
	$(MAKE) -C mip clean
	$(MAKE) -C sweep clean
	@echo "✓ Clean complete"

# Prepare data - parse .dat file and display summary
preprocessing:
	@echo "=== Data Preparation ==="
	@echo "Checking for libutils..."
	@if [ ! -f ../lib/libutils.so ] && [ ! -f ../lib/libutils.dylib ] && [ ! -f ../lib/utils.dll ] && [ ! -f ../bin/utils.dll ]; then \
		echo "libutils not found. Building from src/..."; \
		$(MAKE) -C ../src utils; \
	else \
		echo "✓ libutils found"; \
	fi
	@echo ""
	$(MAKE) -C common preprocessing

# Help target
help:
	@echo "Available targets:"
	@echo "  make all                - Build all modules"
	@echo "  make clean              - Clean all build artifacts"
	@echo "  make preprocessing      - Parse .dat file and prepare data (including distance calculations)"
	@echo "  make help               - Show this help message"

