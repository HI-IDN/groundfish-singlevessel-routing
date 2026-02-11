# Data Preparation - Quick Reference

## Compile and Run Test

### From Repository Root

```bash
cd src-refactor
make test-data-prep
```

### From common/ Directory

```bash
cd src-refactor/common
make run
```

## What It Does

1. **Compiles** `test_data_prep` with:
   - `dat_parser.c` - Parses .dat files
   - `exdata.c` - Builds ExData structures
   - `distance.c` - Computes distance matrices
   - Links with `libDistanceLink.a` from `lib/`

2. **Runs** with:
   - DAT file: `dat/data2023spring.dat`
   - Ship: "Árni Friðriksson"
   - Island data: `dat/island.bin` (auto-loaded by DistanceLink)

3. **Reports**:
   - Number of items loaded (BOAT, STAT, PORT, WAYP)
   - ExData dimensions (SelectedSize, Size)
   - Distance matrix dimensions
   - Sample distances and feasibility stats

## Build Targets

### Root Makefile (`src-refactor/Makefile`)

```bash
make all              # Build all modules
make clean            # Clean all build artifacts
make test-data-prep   # Run data preparation test
make help             # Show help
```

### Common Makefile (`src-refactor/common/Makefile`)

```bash
make all              # Build test_data_prep
make run              # Build and run with data2023spring.dat
make test             # Alias for 'make run'
make clean            # Clean build artifacts
```

## File Structure

```
src-refactor/
├── Makefile                      # Root makefile (orchestrates everything)
├── common/
│   ├── Makefile                  # Builds data prep modules
│   ├── dat_parser.c              # DAT parsing implementation
│   ├── exdata.c                  # ExData building
│   ├── distance.c                # Distance matrix computation
│   ├── test_data_prep.c          # Test program
│   ├── DATA_PREP_README.md       # Full documentation
│   └── REFACTORING_STATUS.md     # Status report
├── include/
│   ├── dat_parser.h              # DAT parsing API
│   ├── exdata.h                  # ExData API
│   └── distance.h                # Distance API
└── ...
```

## Example Session

```bash
$ cd src-refactor
$ make test-data-prep
=== Testing Data Preparation Pipeline ===
make[1]: Entering directory 'src-refactor/common'

Running data preparation test with:
  DAT file: ../../dat/data2023spring.dat
  Ship: "Árni Friðriksson"

=== Data Preparation Test ===

Step 1: Reading .dat file...
  ✓ Loaded 1234 items
  ✓ Ship capacity: 45000
  ✓ SHIP: 1, STAT: 856, PORT: 5 (2 selected), WAYP: 372

Step 2: Building ExData (with selected ports)...
  ✓ SelectedSize: 1231 (ship + stations + selected_ports + waypoints)
  ✓ Size: 859 (ship + stations + selected_ports)

Step 3: Building ExData (no ports)...
  ✓ SelectedSize: 1229 (ship + stations + waypoints)
  ✓ Size: 857 (ship + stations)

Step 4: Building distance matrices...
  ✓ Distance matrix: 1714 × 1714
  ✓ Full distance matrix: 2458 × 2458
  ✓ Applied closure: dist[0,1] = dist[1,0] = 0.0

Step 5: Sampling distances...
  Distance from SHIP-start to STAT[1]-entry: 12.345
  Distance from STAT[1]-exit to STAT[2]-entry: 23.456
  Feasible arcs: 2456789 / 2937796 (83.6%)

=== Test Complete ===
All data preparation steps successful!
```

## Implementation Flow

```
Makefile (root)
    └─> make test-data-prep
            └─> $(MAKE) -C common run
                    └─> common/Makefile
                            ├─> Compile test_data_prep (if needed)
                            │   ├─ dat_parser.c
                            │   ├─ exdata.c
                            │   └─ distance.c
                            └─> Run: ./test_data_prep ../../dat/data2023spring.dat "Árni Friðriksson"
```

## Data Flow Through Test

```
dat/data2023spring.dat
    ↓ read_dat_file()
ItemVec (1234 items)
    ↓ build_exdata()
ExData (Size=859, SelectedSize=1231)
    ↓ build_waypoint_dist()
dist[1714×1714], full_dist[2458×2458]
    ↓ report statistics
Console output
```

## Customization

To test with different data, edit `common/Makefile`:

```makefile
# Data files
DAT_FILE = ../../dat/singleboatdata2023spring.dat  # Change this
SHIP_NAME = "Bjarni Sæmundsson"                    # Or this
```

Or run manually:
```bash
./test_data_prep /path/to/your.dat "Your Ship Name"
```

