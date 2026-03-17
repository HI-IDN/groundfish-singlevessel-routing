# GSP Visualization Scripts

R scripts for visualizing the Groundfish Survey Planning (GSP) solver results and survey data.

## Scripts

### Shared utilities: `plot_utils.R`
Common helper functions used by both plotting scripts:
- `load_required_packages()` - package checks/loading
- `read_db_table()` - SQLite reads with optional query params
- `base_coastline_plot()` - standard coastline layer
- `coord_fixed_for_lat()` - latitude-aware aspect ratio
- `apply_degree_axes()` - degree-formatted axes
- `gsp_common_theme()` - consistent plot theme

This file is sourced automatically by both scripts; you do not run it directly.

### 1. `plot_survey_overview.R`
Visualizes the complete survey setup with all locations.

**What it shows:**
- Coastline of Iceland
- All ports (blue empty circles)
- All boats (red filled circles)
- All trawl stations (blue line segments showing start→end location)

**How to run:**
```bash
Rscript R/plot_survey_overview.R
```

**Output:**
- `dat/survey_overview.png` - High-resolution survey overview map

**Dependencies:**
- `tidyverse` - Data manipulation and visualization (ggplot2, dplyr)
- `DBI` - Database interface
- `RSQLite` - SQLite database connection

---

### 2. `plot_solution_path.R`
Visualizes a single solver solution tour on the map.

**What it shows:**
- Coastline of Iceland
- Tour path connecting visited stations in order
- Color gradient showing tour sequence
- Start location marked with green circle
- Boat location marked with red circle
- Station locations as points along the path

**How to run:**

Basic usage (plots `sol/init_nn.json`):
```bash
Rscript R/plot_solution_path.R
```

Specify a custom solution file:
```bash
Rscript R/plot_solution_path.R sol/init_opt.json
```

**Output:**
- Generates PNG file with same name as input JSON
  - `sol/init_nn.json` → `sol/init_nn.png`
  - `sol/init_opt.json` → `sol/init_opt.png`

**Dependencies:**
- `tidyverse` - Data manipulation and visualization
- `DBI` - Database interface
- `RSQLite` - SQLite database connection
- `jsonlite` - JSON parsing

---

## Installation

### Prerequisites
You must have **R** installed (version 3.6.0 or later).

Install from: https://cran.r-project.org/

### Install Required Packages

Run this in R or RStudio:

```r
packages <- c("tidyverse", "DBI", "RSQLite", "jsonlite")
install.packages(packages)
```

Or from the command line:

```bash
Rscript -e "install.packages(c('tidyverse', 'DBI', 'RSQLite', 'jsonlite'))"
```

### Verify Installation

Test that all dependencies are available:

```bash
Rscript -e "library(tidyverse); library(DBI); library(RSQLite); library(jsonlite); cat('✓ All packages loaded successfully\n')"
```

---

## Workflow

### Step 1: Data Preparation
First, create the database from raw .dat file:

```bash
make -C src country
make -C src stations
make -C src distance
```

This creates `dat/gsp_data.db` and `dat/survey_overview.png`

### Step 2: Run Solver
Run the GSP solver to generate solutions:

```bash
./build/gsp --mode init --strategy nn \
  --database dat/gsp_data.db \
  --config config/gsp_solver.yaml \
  --output sol/init_nn.json
```

This creates `sol/init_nn.json`

### Step 3: Visualize Results
Plot the solution:

```bash
Rscript R/plot_solution_path.R sol/init_nn.json
```

This creates `sol/init_nn.png`

---

## Database Schema

These scripts read from `dat/gsp_data.db` with the following tables:

### `locations`
| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER | Location unique ID |
| type | INT | 0=Boat, 1=Station, 2=Port, 3=Waypoint |
| lat | REAL | Latitude (decimal degrees) |
| lon | REAL | Longitude (decimal degrees) |

### `boats`
| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER | Boat unique ID |
| name | TEXT | Boat name |
| capacity | INT | Vessel capacity |
| location_id | INTEGER | Starting location |

### `stations`
| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER | Station unique ID |
| amount | REAL | Catch amount |
| start_location_id | INTEGER | Haul start location |
| end_location_id | INTEGER | Haul end location |

### `ports`
| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER | Port unique ID |
| name | TEXT | Port name |
| selected | INTEGER | 1 if active, 0 if inactive |
| location_id | INTEGER | Port location |

### `coastline`
| Column | Type | Description |
|--------|------|-------------|
| lat | REAL | Coastline point latitude |
| lon | REAL | Coastline point longitude |

---

## Solution JSON Format

Solution files (`sol/init_*.json`) have this structure:

```json
{
  "metadata": {
    "solver_version": "1.0.0",
    "timestamp": "1709650200",
    "mode": "init",
    "strategy": "nn",
    "boat_id": 2
  },
  "problem": {
    "num_nodes": 581,
    "num_stations": 580
  },
  "solution": {
    "tour": [0, 42, 157, 88, ..., 305],
    "tour_length": 581,
    "total_distance_nm": 5943.37,
    "feasible": true
  },
  "solver_stats": {
    "status": "completed",
    "runtime_seconds": 0.010,
    "method": "nn_heuristic"
  }
}
```

**Fields:**
- `tour`: Array of node indices visited in order (0 = boat, 1+ = stations)
- `total_distance_nm`: Total tour distance in nautical miles
- `runtime_seconds`: Solver execution time

---

## Troubleshooting

### Error: "Package 'X' is required but not installed"
Install missing packages:
```bash
Rscript -e "install.packages('X')"
```

### Error: "Database file not found"
Make sure preprocessing has been run:
```bash
cd build && make preprocess && cd ..
```

### Error: "Solution file not found"
Run the solver first:
```bash
./build/gsp --mode init --strategy nn \
  --database dat/gsp_data.db \
  --config config/gsp_solver.yaml
```

### Plot looks wrong or has missing data
Check that:
1. Database exists: `ls -la dat/gsp_data.db`
2. Solution file exists and is valid JSON: `cat sol/init_nn.json | head -20`
3. All required tables have data: Open DB and check `SELECT COUNT(*) FROM coastline;`

---

## Advanced Usage

### Compare Multiple Solutions
Plot all strategies side-by-side:

```bash
# Generate all solutions
for strategy in nn ge ci opt; do
  ./build/gsp --mode init --strategy $strategy \
    --database dat/gsp_data.db \
    --config config/gsp_solver.yaml
  
  # Plot each
  Rscript R/plot_solution_path.R sol/init_${strategy}.json
done

# View results
ls -lah sol/init_*.png
```

### Customize Plots
Edit the R scripts to change:
- Colors: Modify `scale_color_viridis_c()` calls
- Line widths: Change `linewidth` parameters
- Point sizes: Modify `size` parameters
- Title/labels: Edit `labs()` section

---

## Performance Notes

- **plot_survey_overview.R**: ~5 seconds (full coastline rendering)
- **plot_solution_path.R**: ~10 seconds (with JSON parsing)
- Output PNG files are 300 DPI and ~2-5 MB

---

## Contact & Issues

For issues with visualization scripts, check:
1. Database integrity: `sqlite3 dat/gsp_data.db ".tables"`
2. Solution JSON format: `jq . sol/init_nn.json`
3. R package versions: `packageVersion("tidyverse")`
