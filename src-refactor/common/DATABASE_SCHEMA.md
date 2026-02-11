# Database Schema

**Database**: `dat/gsp_data.db` (SQLite)

## Overview

Normalized relational schema for Icelandic groundfish survey data. All geographic coordinates stored in central `locations` table. Station/port assignments to boats tracked via `survey_2023` table (allows multiple surveys without duplicating station data).

## Source Data Format

Original `.dat` file format:
```
BOAT 663381 224128 640278 220834 14000 10 4 30 255 153 153 "Bjarni Sæmundsson"
STAT 473 2 0 650029 235012 645886 235565 265 24  #  Rifnaði eftir 2,3 sml \\ botndypi_kastad= 229 botndypi_hift= 222 \\
PORT 635500 223300 "Ísafjörður" 1
WAYP 640150 224730 0
```

## Complete Schema

### locations (Raw Storage)
All geographic coordinates stored once in raw degmin format.

```sql
CREATE TABLE locations (
    id INTEGER PRIMARY KEY,
    type TEXT CHECK(type IN ('B', 'S1', 'S2', 'P', 'W')),
    lat_degmin REAL,    -- Original format: 640278
    lon_degmin REAL     -- Original format: 220834
);
```

**Location Types:**
- `B` - Boat starting location
- `S1` - Station start (where net is thrown/cast)
- `S2` - Station end (where net is hauled)
- `P` - Port location
- `W` - Waypoint (shared across all surveys)

### v_locations (Computed View)
Converts degmin to decimal degrees on-the-fly.

```sql
CREATE VIEW v_locations AS 
SELECT 
    id, type, lat_degmin, lon_degmin,
    (lat_degmin + (200.0/3.0) * ((lat_degmin/100.0) - CAST(lat_degmin/10000.0 AS INTEGER) * 100.0)) / 10000.0 AS lat_deg,
    (lon_degmin + (200.0/3.0) * ((lon_degmin/100.0) - CAST(lon_degmin/10000.0 AS INTEGER) * 100.0)) / 10000.0 AS lon_deg
FROM locations;
```

**Conversion example:** `640278` → `64.046333°`

### boats
```sql
CREATE TABLE boats (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,     -- Quotes stripped: "Árni Friðriksson" → Árni Friðriksson
    capacity REAL,
    location_id INTEGER REFERENCES locations(id)
);
```

### stations (Pure Data - No Assignment)
No `boat_id`, no `reitur`/`tog` - just station characteristics.

```sql
CREATE TABLE stations (
    id INTEGER PRIMARY KEY,
    amount REAL,                   -- Catch amount
    start_location_id INTEGER REFERENCES locations(id),
    end_location_id INTEGER REFERENCES locations(id),
    depth_thrown INTEGER,          -- botndypi_kastad (m)
    depth_haul INTEGER,            -- botndypi_hift (m)
    comment TEXT                   -- Cleaned comment
);
```

**Comment Parsing:**
```
Input:  "#  Rifnaði eftir 2,3 sml \\ botndypi_kastad= 229 botndypi_hift= 222 \\"
Output: depth_thrown=229, depth_haul=222, comment="Rifnaði eftir 2,3 sml"
```

### ports (Pure Data - No Assignment)
```sql
CREATE TABLE ports (
    id INTEGER PRIMARY KEY,
    name TEXT,              -- Quotes stripped
    selected INTEGER,       -- 0 or 1
    location_id INTEGER REFERENCES locations(id)
);
```

### waypoints (Shared)
```sql
CREATE TABLE waypoints (
    id INTEGER PRIMARY KEY,
    location_id INTEGER REFERENCES locations(id)
);
```

### survey_2023 (Assignment & Routing)
Maps boats to stations/ports with routing order.

```sql
CREATE TABLE survey_2023 (
    id INTEGER PRIMARY KEY,
    boat_id INTEGER REFERENCES boats(id),
    location_type TEXT CHECK(location_type IN ('S', 'P')),  -- S=Station, P=Port
    location_id INTEGER,     -- FK to stations.id or ports.id
    order_num INTEGER        -- Route sequence
);
```

**Design Rationale:** Separating assignment from data allows:
- Multiple surveys to reference same stations
- Easy route reordering
- Stations can be shared across boats (future)

### metadata
```sql
CREATE TABLE metadata (
    key TEXT PRIMARY KEY,
    value TEXT
);
```

Stores: `source_file`, `import_time`

## Query Examples

### Get all boats with their locations (decimal degrees)
```sql
SELECT b.id, b.name, b.capacity, v.lat_deg, v.lon_deg
FROM boats b
JOIN v_locations v ON b.location_id = v.id;
```

### Get boat's route in order with station details
```sql
SELECT 
    b.name AS boat,
    sv.order_num,
    sv.location_type,
    s.amount,
    s.depth_thrown,
    s.depth_haul,
    s.comment,
    v1.lat_deg AS start_lat,
    v1.lon_deg AS start_lon,
    v2.lat_deg AS end_lat,
    v2.lon_deg AS end_lon
FROM survey_2023 sv
JOIN boats b ON sv.boat_id = b.id
JOIN stations s ON sv.location_id = s.id AND sv.location_type = 'S'
JOIN v_locations v1 ON s.start_location_id = v1.id
JOIN v_locations v2 ON s.end_location_id = v2.id
WHERE b.id = 1
ORDER BY sv.order_num;
```

### Get station with depths and coordinates
```sql
SELECT 
    s.id,
    s.amount,
    s.depth_thrown,
    s.depth_haul,
    s.comment,
    v1.lat_deg || ',' || v1.lon_deg AS start_coords,
    v2.lat_deg || ',' || v2.lon_deg AS end_coords
FROM stations s
JOIN v_locations v1 ON s.start_location_id = v1.id
JOIN v_locations v2 ON s.end_location_id = v2.id
WHERE s.id = 1;
```

### Count items by type
```sql
SELECT type, COUNT(*) 
FROM locations 
GROUP BY type;
```

Expected output:
```
B  |   4   (boats)
S1 | 580   (station starts)
S2 | 580   (station ends)
P  |  30   (ports)
W  |  27   (waypoints)
```

### Get all waypoints with coordinates
```sql
SELECT w.id, v.lat_deg, v.lon_deg
FROM waypoints w
JOIN v_locations v ON w.location_id = v.id;
```

## Key Design Features

1. **Normalized**: No coordinate duplication
2. **Raw + Computed**: Raw degmin in table, decimal degrees in view
3. **No boat_id in stations/ports**: Assignment via `survey_2023`
4. **Depth extraction**: Automatically parsed from comments
5. **Clean comments**: Depth metadata removed
6. **Quote stripping**: Names cleaned
7. **UTF-8 support**: Icelandic characters handled correctly

## Usage

### Create Database
```bash
cd src-refactor
make prep-data
```

### Force Recreate
```bash
make prep-data-force
```

### Query Database
```bash
sqlite3 dat/gsp_data.db
```

## Files

- **Source**: `src-refactor/common/prep_data.c`
- **Database**: `dat/gsp_data.db`
- **Input**: `dat/data2023spring.dat`

---
*Schema Version: 2.0 - Normalized with survey assignment table*  
*Last Updated: 2026-02-11*


## Schema Design

### locations table (Central Repository)
```sql
CREATE TABLE locations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    type TEXT CHECK(type IN ('B', 'S1', 'S2', 'P', 'W')),
    lat_degmin REAL,      -- Original degmin format
    lon_degmin REAL,      -- Original degmin format  
    lat_deg REAL,         -- Decimal degrees (computed)
    lon_deg REAL          -- Decimal degrees (computed)
);
```

**Location Types:**
- `'B'` - Boat starting location
- `'S1'` - Station start location (entry point)
- `'S2'` - Station end location (exit point)
- `'P'` - Port location
- `'W'` - Waypoint location

### boats table
```sql
CREATE TABLE boats (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,          -- Quotes stripped from .dat file
    capacity REAL NOT NULL,
    location_id INTEGER,         -- FK to locations
    FOREIGN KEY (location_id) REFERENCES locations(id)
);
```

### stations table
```sql
CREATE TABLE stations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    boat_id INTEGER,
    reitur INTEGER,               -- Station external ID
    tog INTEGER,                  -- Station sub-ID
    amount REAL,                  -- Catch amount
    start_location_id INTEGER,    -- FK to locations (type='S1')
    end_location_id INTEGER,      -- FK to locations (type='S2')
    FOREIGN KEY (boat_id) REFERENCES boats(id),
    FOREIGN KEY (start_location_id) REFERENCES locations(id),
    FOREIGN KEY (end_location_id) REFERENCES locations(id)
);
```

**Key Change**: Stations now have TWO location references (start and end) instead of embedding coordinates directly.

### ports table
```sql
CREATE TABLE ports (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    boat_id INTEGER,
    name TEXT,                    -- Quotes stripped
    selected INTEGER,             -- 0 or 1
    location_id INTEGER,
    FOREIGN KEY (boat_id) REFERENCES boats(id),
    FOREIGN KEY (location_id) REFERENCES locations(id)
);
```

### waypoints table
```sql
CREATE TABLE waypoints (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    location_id INTEGER,
    FOREIGN KEY (location_id) REFERENCES locations(id)
);
```

### metadata table
```sql
CREATE TABLE metadata (
    key TEXT PRIMARY KEY,
    value TEXT
);
```

Stores:
- `source_file` - Original .dat filename
- `import_time` - Timestamp of import

### v_locations view
```sql
CREATE VIEW v_locations AS 
SELECT id, type, lat_degmin, lon_degmin, lat_deg, lon_deg 
FROM locations;
```

Convenience view for querying locations.

## Benefits

1. **No Data Duplication** - Each geographic coordinate stored once
2. **Referential Integrity** - Foreign keys ensure data consistency
3. **Easy Queries** - Join to locations for geographic data
4. **Flexible** - Easy to add new location-based entities
5. **Clean** - Names have quotes stripped automatically

## Usage

### First Run - Create Database
```bash
make prep-data
# Creates dat/gsp_data.db
```

### Second Run - Error (Database Exists)
```bash
make prep-data
# Error: Database already exists. Use --force to recreate.
```

### Force Recreate
```bash
make prep-data-force
# Drops all tables and recreates from scratch
```

## Query Examples

### Get All Boat Locations
```sql
SELECT b.name, b.capacity, l.lat_deg, l.lon_deg, l.type
FROM boats b
JOIN locations l ON b.location_id = l.id;
```

### Get Station with Start/End Coordinates
```sql
SELECT 
    s.reitur, s.tog, s.amount,
    l1.lat_deg AS start_lat, l1.lon_deg AS start_lon,
    l2.lat_deg AS end_lat, l2.lon_deg AS end_lon
FROM stations s
JOIN locations l1 ON s.start_location_id = l1.id
JOIN locations l2 ON s.end_location_id = l2.id
WHERE s.boat_id = 1;
```

### Get All Waypoints
```sql
SELECT w.id, l.lat_deg, l.lon_deg
FROM waypoints w
JOIN locations l ON w.location_id = l.id;
```

### Count Locations by Type
```sql
SELECT type, COUNT(*) as count
FROM locations
GROUP BY type;
```

Expected output:
```
B  | 4    (boats)
S1 | 580  (station starts)
S2 | 580  (station ends)
P  | 30   (ports)
W  | 27   (waypoints)
```

## Data Flow

```
.dat file
    ↓ parse
ItemVec (raw)
    ↓ normalize
locations table ← boats, stations, ports, waypoints (via FK)
```

## Files Modified

- `prep_data.c` - Complete rewrite with normalized schema
- `Makefile` - Added `prep-force` target  
- `src-refactor/Makefile` - Added `prep-data-force` target
- `SQLITE_INTEGRATION.md` - Updated documentation

## Implementation Details

### Quote Stripping
```c
char* strip_quotes(const char *name) {
    // Removes leading/trailing quotes from names
    // "Árni Friðriksson" → Árni Friðriksson
}
```

### Degree Conversion
Uses existing `degmin2deg()` function to convert:
- `640278` (degmin) → `64.046333` (decimal degrees)

### Force Recreate Logic
1. Check if `boats` table exists
2. If exists and `--force` not set → Error
3. If exists and `--force` set → Drop all tables
4. Create fresh schema
5. Import data

## Testing

```bash
# Clean build
make clean
make prep-data

# Should create database successfully

# Try again (should fail)
make prep-data
# Error: Database already exists

# Force recreate
make prep-data-force
# Success: Tables dropped and recreated
```

## Next Steps

1. ✅ Normalized schema implemented
2. ✅ Quote stripping implemented
3. ✅ Decimal degrees conversion
4. ✅ Force recreate flag
5. TODO: Add indexes for performance
6. TODO: Add distance matrix caching
7. TODO: Create query helper functions

---
**Date**: 2026-02-11  
**Status**: COMPLETE ✓  
**Database**: `dat/gsp_data.db` (normalized schema)

