Preprocessing: Parse DAT to SQLite
===================================

Overview
--------

The preprocessing stage reads the raw `.dat` input file (e.g., `data2023spring.dat`) and populates a SQLite database with structured tables for locations, ports, vessels, stations, and waypoints. This standardized database format enables efficient downstream processing by initialization and sweep stages.

Input Format: `.dat` Files
---------------------------

The `.dat` file format is plain-text with four record types:

1. **PORT** — Harbor/port location
   ```
   PORT <lat> <lon> [<name>] [<flag>]
   Example: PORT 632700 201600 "Vestmannaeyjar" 1
   ```

2. **BOAT** — Vessel with start/end locations and capacity
   ```
   BOAT <lat1> <lon1> <lat2> <lon2> <capacity> [c1 c2 c3 c4 c5 c6] [<name>]
   Example: BOAT 640278 220834 640278 220834 45000 10 4 30 153 153 255 "Árni Friðriksson"
   ```

3. **STAT** — Survey station with two coordinate pairs
   ```
   STAT <ext_id> [<subid>] [<flag>] <lat1> <lon1> <lat2> <lon2> [# comment ...]
   Example: STAT 367 31 0 633542 174312 633136 174051 # botndypi_kastad=108
   ```

4. **WAYP** — Waypoint (navigational aid or land-crossing route)
   ```
   WAYP <lat> <lon> [<flag>]
   Example: WAYP 663381 224128 1
   ```

Output: SQLite Schema
---------------------

The preprocessor creates the following tables:

### Table: `locations`

All unique geographic points (ports, stations, waypoints).

```sql
CREATE TABLE locations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    type TEXT NOT NULL,        -- 'P' (port), 'S1'/'S2' (station), 'W' (waypoint), 'B' (boat start/end)
    lat INTEGER NOT NULL,       -- Integer coordinate (degmin format)
    lon INTEGER NOT NULL       -- Integer coordinate (degmin format)
);
```

### Table: `ports`

Ports and harbors.

```sql
CREATE TABLE ports (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    location_id INTEGER NOT NULL,
    name TEXT,
    flag INTEGER,
    FOREIGN KEY (location_id) REFERENCES locations(id)
);
```

### Table: `boats`

Vessels with capacity and characteristics.

```sql
CREATE TABLE boats (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    start_location_id INTEGER NOT NULL,
    end_location_id INTEGER NOT NULL,
    capacity INTEGER NOT NULL,
    c1 INTEGER, c2 INTEGER, c3 INTEGER,  -- Extra columns (purpose TBD)
    c4 INTEGER, c5 INTEGER, c6 INTEGER,
    name TEXT,
    FOREIGN KEY (start_location_id) REFERENCES locations(id),
    FOREIGN KEY (end_location_id) REFERENCES locations(id)
);
```

### Table: `stats` (aka `stations`)

Survey stations.

```sql
CREATE TABLE stats (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ext_id INTEGER NOT NULL,     -- External identifier (unique)
    subid INTEGER,
    flag INTEGER,
    start_location_id INTEGER NOT NULL,  -- S1 coordinate
    end_location_id INTEGER NOT NULL,    -- S2 coordinate
    botndypi_kastad INTEGER,     -- Extracted from comment
    botndypi_hift INTEGER,       -- Extracted from comment
    remark TEXT,                 -- Remaining comment text
    FOREIGN KEY (start_location_id) REFERENCES locations(id),
    FOREIGN KEY (end_location_id) REFERENCES locations(id)
);
```

### Table: `waypoints`

Waypoints (navigational aids).

```sql
CREATE TABLE waypoints (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    location_id INTEGER NOT NULL,
    flag INTEGER,
    FOREIGN KEY (location_id) REFERENCES locations(id)
);
```

### Table: `metadata`

Pipeline metadata and processing status.

```sql
CREATE TABLE metadata (
    key TEXT PRIMARY KEY,
    value TEXT,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

CLI Usage
---------

### Basic Invocation

```bash
./bin/preprocess \
  --input dat/data2023spring.dat \
  --db dat/parsed_data.sqlite \
  --log-level info
```

### Arguments

- `--input <path>` — Path to `.dat` input file (required)
- `--db <path>` — Path to output SQLite database (required; created if doesn't exist)
- `--overwrite` — Overwrite existing database (default: append mode)
- `--log-level {debug|info|warn|error}` — Logging verbosity (default: info)
- `--validate` — Run schema validation after parsing (default: true)
- `--compute-coords` — Convert integer degmin to decimal degrees (default: true)

### Examples

**First-time preprocessing:**
```bash
./bin/preprocess \
  --input dat/data2023spring.dat \
  --db dat/parsed_data.sqlite \
  --overwrite \
  --log-level debug
```

**Append to existing database:**
```bash
./bin/preprocess \
  --input dat/new_data.dat \
  --db dat/parsed_data.sqlite \
  --log-level info
```

Output & Logging
----------------

### Standard Output

```
[INFO] Preprocessing: dat/data2023spring.dat → dat/parsed_data.sqlite
[INFO] Opening database: dat/parsed_data.sqlite
[INFO] Parsing input file...
[INFO]   Records parsed: 8/8
[INFO]   Locations: 325 unique points
[INFO]   Ports: 12
[INFO]   Boats: 1
[INFO]   Stations: 150
[INFO]   Waypoints: 8
[INFO] Computing decimal coordinates...
[INFO] Validating schema...
[INFO] Preprocessing complete. Runtime: 0.23 seconds
```

### Database Schema Validation

The `--validate` flag checks:
- No NULL foreign keys
- No orphaned location records
- Station coordinates are distinct (S1 ≠ S2)
- All ports have valid locations
- All boats have valid start/end locations

Implementation Details
----------------------

### Tokenization

The parser tokenizes input lines by:
1. Replacing commas with spaces (both are separators)
2. Recognizing quoted strings (single or double quotes)
3. Handling backslash escapes inside quoted strings
4. Stripping comments (text after `#`)

### Coordinate Encoding

- **Input:** Integers in degmin format (e.g., 632700 = 63°27'00")
- **Storage:** Raw integers (efficient, preserves precision)
- **Conversion:** Decimal degrees for downstream analysis (lat_decimal, lon_decimal) via view 
  `v_locations`.

Formula for degmin → decimal:
```
degree = (degmin + (200/3) * (degmin % 100)) / 10000
latitude = degree (if positive)
latitude = -degree (if negative)
```

### Parser State Machine

```
for each line:
  if line starts with "PORT":
    parse PORT record
    create locations entry (type='P')
    create ports entry
  else if line starts with "BOAT":
    parse BOAT record
    create two locations entries (type='B', start and end)
    create boats entry
  else if line starts with "STAT":
    parse STAT record
    create two locations entries (type='S1' and 'S2')
    extract botndypi_* from comment
    create stats entry
  else if line starts with "WAYP":
    parse WAYP record
    create locations entry (type='W')
    create waypoints entry
  else:
    skip (comment or unknown line)
```

References
----------
- Paper: "Groundfish Survey Routing: A Scalable Matheuristic"

