# Data Preparation Module

## Overview

This module implements the data preparation pipeline following these steps:

1. **Parse .dat file** → ItemVec (raw items)
2. **Build ExData** → Expanded representation for optimization
3. **Compute distance matrices** → Waypoint-aware routing

## Files

### Headers (`include/`)

- **`dat_parser.h`** - DAT file parsing, tokenization, coordinate conversion
- **`exdata.h`** - ExData structure and building functions
- **distance.h`** - Distance matrix computation with waypoint routing

### Implementation (`common/`)

- **`math.c`** → renamed to **`dat_parser.c`** - DAT file parsing implementation
- **`output.c`** → renamed to **`exdata.c`** - ExData building implementation
- **`db_helpers.c`** → renamed to **`distance.c`** - Distance matrix implementation

## Data Flow

```
┌─────────────┐
│ .dat file   │
└──────┬──────┘
       │ read_dat_file()
       ▼
┌─────────────┐
│  ItemVec    │  Raw list of BOAT, STAT, PORT, WAYP
└──────┬──────┘
       │ build_exdata() or build_exdata_no_ports()
       ▼
┌─────────────┐
│   ExData    │  Expanded: ship, stations, ports, waypoints
└──────┬──────┘
       │ build_waypoint_dist()
       ▼
┌─────────────┐
│ dist, fsb   │  Distance & feasibility matrices (2*Size × 2*Size)
│ full_dist   │  Full matrices (2*SelectedSize × 2*SelectedSize)
└─────────────┘
```

## Usage Example

```c
#include "dat_parser.h"
#include "exdata.h"
#include "distance.h"

int main() {
    /* Step 1: Read .dat file */
    ItemVec items;
    item_vec_init(&items);
    double ship_cap = 0.0;
    read_dat_file("data.dat", "Árni Friðriksson", &items, &ship_cap, 0);
    
    /* Step 2: Build ExData */
    ExData ex = build_exdata(&items);  /* with selected ports */
    /* or: ExData ex = build_exdata_no_ports(&items); */
    
    /* Step 3: Build distance matrices */
    double *dist = NULL;
    int *fsb = NULL;
    double *full_dist = NULL;
    int *full_fsb = NULL;
    int full_m = 0;
    
    build_waypoint_dist(&ex, NULL, 0, &dist, &fsb, 
                        &full_dist, &full_fsb, &full_m);
    
    /* Special closure: nodes 0 and 1 (ship start/end) */
    int n = 2 * ex.Size;
    dist[0*n + 1] = 0.0;
    dist[1*n + 0] = 0.0;
    fsb[0*n + 1] = 1;
    fsb[1*n + 0] = 1;
    
    /* Use dist, fsb for optimization... */
    
    /* Cleanup */
    free(dist);
    free(fsb);
    free(full_dist);
    free(full_fsb);
    free_exdata(&ex);
    item_vec_free(&items);
    
    return 0;
}
```

## Data Structures

### Item (from .dat file)

```c
typedef struct {
    int Type;              /* tSHIP, tSTAT, tPORT, tWAYP */
    int Fixed;             /* Station is fixed orientation */
    int Rotated;           /* Station can be rotated */
    double LatLonRad[4];   /* [start_lat, start_lon, end_lat, end_lon] radians */
    double LatLonDegMin[4];/* Same, in degmin format */
    char *Name;
    char *RawLine;         /* Original line from file */
    char *Comment;         /* Comment portion (if any) */
    int Reitur;            /* Station external ID */
    int Tog;               /* Station sub-ID */
    int PortSelected;      /* Port is selected for use */
    double BoatData[11];   /* Boat parameters */
    int BoatDataLen;
    double Amount;         /* Catch amount */
    double ExtraTime;      /* Extra time at station */
} Item;
```

### ItemVec (dynamic array)

```c
typedef struct {
    Item *a;    /* Array of items */
    int n;      /* Current count */
    int cap;    /* Capacity */
} ItemVec;
```

### ExData (expanded representation)

```c
typedef struct {
    int SelectedSize;      /* ship + stations + selected_ports + waypoints */
    int Size;              /* ship + stations + selected_ports (no waypoints) */
    int *Type;             /* [SelectedSize] */
    int *ItemIndex;        /* [SelectedSize] index into ItemVec */
    double *Amount;        /* [SelectedSize] */
    double *LatLonRad;     /* [SelectedSize][4] */
    double *LatLonDegMin;  /* [SelectedSize][4] */
} ExData;
```

## Order Convention

**ItemVec order**: Arbitrary (as read from file)

**ExData order**: 
1. Ship (1 item)
2. All stations
3. Selected ports (if using `build_exdata`)
4. All waypoints

This ordering is critical for the 2-node-per-location model:
- Node `2*i` = entry to location i
- Node `2*i + 1` = exit from location i

## Coordinate Formats

### DegMin Format
Used in .dat files: `DDDDMM` where `DDDD` = degrees * 100, `MM` = minutes

Example: `640278` = 64° 02.78'

### Conversion Functions
- `degmin2rad(degmin)` - Convert degmin → radians (for distance computation)
- `degmin2deg(degmin)` - Convert degmin → decimal degrees (for display)

Formula:
```
minutes = (degmin / 100) - floor(degmin / 10000) * 100
degrees = (degmin + (200/3) * minutes) / 10000
radians = degrees * π / 180
```

## Distance Matrix

The `build_waypoint_dist()` function calls the external `DistanceLink` function (from `libutils` built from `src/utils.c`) which:

1. Computes great-circle distances between all location pairs
2. Checks for land crossings using `island.bin`
3. Routes around land using waypoints (Dijkstra's algorithm)
4. Returns feasibility and distance matrices

**Output matrices:**
- `dist[n][n]` - Distance matrix for main route (n = 2 * Size)
- `fsb[n][n]` - Feasibility matrix (1 = feasible, 0 = infeasible)
- `full_dist[M][M]` - Full matrix including waypoints (M = 2 * SelectedSize)
- `full_fsb[M][M]` - Full feasibility matrix

## Implementation Notes

### Exact Logic from matcapmutheur_v3.c

This refactored implementation follows the original logic exactly:

1. **Tokenization** - Same regex-like tokenizer handling quoted strings
2. **Parsing** - Same state machine (tag 0/1 for WAYPONLY mode)
3. **ExData building** - Same ordering: ship → stats → ports → waypoints
4. **Distance computation** - Same DistanceLink call signature and matrix extraction

### Memory Management

All functions use `xmalloc`/`xcalloc` which die on OOM. Callers must:
- Call `item_vec_free()` to free ItemVec
- Call `free_exdata()` to free ExData
- Call `free()` on distance matrices

### Error Handling

Functions die with error message on:
- File not found
- Ship name mismatch
- DistanceLink failure
- OOM

## Testing

### Quick Start

**From repository root:**
```bash
make test-data-prep
```

**From common/ directory:**
```bash
cd src-refactor/common
make run
```

Both commands will:
1. Compile test_data_prep with all dependencies
2. Run with `dat/data2023spring.dat` and ship "Árni Friðriksson"
3. Display data preparation statistics

**Expected output:**
- ItemVec size (BOAT + STAT + PORT + WAYP counts)
- ExData SelectedSize and Size
- Distance matrix dimensions
- Sample distances and feasibility statistics

## Future Enhancements

- [ ] Add unit tests for each module
- [ ] Add validation functions (check for duplicates, invalid coords, etc.)
- [ ] Add progress callbacks for large files
- [ ] Support multiple ships in single .dat file
- [ ] Cache parsed data in SQLite database

