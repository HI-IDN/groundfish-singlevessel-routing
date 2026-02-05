dat file format
===============================================

This document explains the plain‑text `.dat` format used in this repository (the
`data2023spring.dat` input). The authoritative parser is `src/parse_dat_to_sqlite.c` — the README
below describes exactly how that C parser interprets lines and tokens so you can safely edit or
extend data files.

Quick summary
-------------

- Lines beginning with the keywords `PORT`, `BOAT`, `STAT`, `WAYP` (case‑sensitive) are parsed.
  Other lines and comments are ignored.
- Tokens are whitespace (or comma) separated; names may be quoted with single or double quotes.
  Backslashes inside quoted strings are treated as escapes and are removed.
- A `#` starts a line comment; the parser strips the comment text (and uses it for STAT remark
  fields and to extract key=value pairs like `botndypi_kastad=NNN`).
- Lat/lon coordinates in the `.dat` file are integer values (the project uses degmin-style integer
  encodings; the codebase contains helpers to convert to radians/decimal degrees where needed).

Tokenization rules (precise)
----------------------------

- The parser first replaces commas with spaces so both commas and whitespace are treated as
  separators.
- A token may be quoted with `"` or `'`. Inside quoted tokens a backslash `\` escapes the next
  character; escape sequences are evaluated by the parser and the backslashes removed.
- Unquoted tokens extend until the next whitespace (or end of line).
- Anything after a `#` is considered a comment (the parser keeps the comment string separately and
  may extract key=value pairs from it).

Recognized line types and fields
--------------------------------
The parser recognizes four main record types. Below each type is shown as an ordered list of tokens
the parser expects. Square brackets show optional tokens.

1) PORT
    - Syntax: PORT <lat> <lon> [<name>] [<flag>]
    - Example:
      `PORT 632700 201600 "Vestmannaeyjar" 1`
    - Meaning:
        - <lat>, <lon>: integer coordinates (stored as integers in DB). See notes on coordinate
          encoding below.
        - <name>: optional token (may be quoted); stored as `ports.name`.
        - <flag>: optional integer (stored as `ports.flag`).
    - Parser action: creates a `locations` row with `type = "P"` and inserts a `ports` row
      referencing that location id.

2) BOAT
    - Syntax: BOAT <lat1> <lon1> <lat2> <lon2> <capacity> [c1 c2 c3 c4 c5 c6] [<name>]
    - Example (trimmed):
      `BOAT 640278 220834 640278 220834 45000 10 4 30 153 153 255 "Árni Friðriksson"`
    - Meaning:
        - <lat1>,<lon1>: start coordinate for the boat (integer)
        - <lat2>,<lon2>: end coordinate for the boat (integer)
        - <capacity>: integer, vessel capacity
        - c1..c6: six integer columns (purpose not explicitly named in the parser — stored as
          integer columns c1..c6 in `boats` table)
        - <name>: optional quoted name string (stored in `boats.name`)
    - Parser action: inserts two `locations` rows with `type = "B"` (start and end); inserts a
      `boats` row referencing those two location ids and the capacity/extra columns.

3) STAT
    - Syntax: STAT <ext_id> [<subid>] [<flag>] <lat1> <lon1> <lat2> <lon2>  [# comment ...]
    - Example (trimmed):
      `STAT 367 31 0 633542 174312 633136 174051 922 15 # \\ botndypi_kastad= 108 botndypi_hift=120 \\` 
    - Meaning:
        - <ext_id>: integer external id (unique identifier for the station as recorded in the
          dataset)
        - <lat1>,<lon1>: first coordinate for the station (type S1)
        - <lat2>,<lon2>: second coordinate for the station (type S2)
        - Optional fields (subid, flag) are read by the tokenizer but the parser tolerates their
          presence/absence; the important coordinates are lat/long tokens.
        - Comment (after `#`) is used for two purposes:
            - If the comment contains `botndypi_kastad=NNN` or `botndypi_hift=NNN` these integer
              keys are parsed out and stored into stat columns `botndypi_kastad` and
              `botndypi_hift`. The parser removes those key=value substrings from the remaining
              remark text.
            - The remaining comment text (after removing the key=value pieces and backslashes) is
              stored in the stats `remark` column as a string (or NULL if blank).
    - Parser action: creates two location rows with `type = "S1"` and `type = "S2"`, inserts a
      `stats` row with `ext_id` and references to start/end location ids, plus any extracted keys
      and remark.

4) WAYP
    - Syntax: WAYP <lat> <lon> [<flag>]
    - Example:
      `WAYP 663381 224128 1`
    - Meaning:
        - <lat>,<lon: waypoint coordinates (integer)
        - <flag>: optional integer
    - Parser action: creates a `locations` row with `type = "W"` and inserts into `waypoints` table
      with `flag`.

Other line types are ignored by the parser.

Coordinate encoding and conversion
----------------------------------

- The parser stores the latitude/longitude numbers as integers in the DB `locations(lat,lon)`
  columns. The rest of the codebase contains helpers (see `py/survey_utils.py`) that convert these
  integers to radians/decimal degrees and then to plotting coordinates.
- In `py/survey_utils.py` there are functions `degmin2rad`, `degmin2deg` and `deg2point` which
  indicate the project uses a deg/min style integer encoding sometimes used in nautical datasets.
  When plotting or computing great‑circle distances the Python helpers convert the stored int values
  accordingly.

Comment handling details
------------------------

- Any text after a `#` is copied into a comment buffer. The parser then calls `extract_int_key()` to
  search for keys `botndypi_kastad` and `botndypi_hift` (exact names). If found, the integer value
  is extracted and removed from the comment string; the remaining comment (with backslashes removed
  and whitespace compressed) becomes `stats.remark` if nonempty.
- Backslashes present in comments are removed and act as escape/line‑continuation markers prior to
  storing remarks.

Database output schema (what the parser creates)
------------------------------------------------
The parser creates `outdir/parsed_data.sqlite` (by default `dat/parsed_data.sqlite`) with the
following tables:

- locations(id INTEGER PRIMARY KEY, lat INTEGER, lon INTEGER, type TEXT)
    - A unique index on (lat,lon,type).
    - `type` values used: `P` (port), `B` (boat location), `S1` (station start), `S2` (station end),
      `W` (waypoint).

- boats(id INTEGER PRIMARY KEY, start_loc INTEGER, end_loc INTEGER, capacity INTEGER, c1..c6
  INTEGER, name TEXT, FOREIGN KEYs -> locations)

- ports(id INTEGER PRIMARY KEY, loc_id INTEGER, name TEXT, flag INTEGER, FOREIGN KEY(loc_id) ->
  locations(id))

- stations(id INTEGER PRIMARY KEY, ext_id INTEGER, start_loc INTEGER, end_loc INTEGER, remark TEXT,
  bottom_depth_cast INTEGER, bottom_depth_haul INTEGER, FOREIGN KEYs -> locations)

- waypoints(id INTEGER PRIMARY KEY, loc_id INTEGER, flag INTEGER, FOREIGN KEY(loc_id) -> locations(
  id))

- distances(i INTEGER NOT NULL, j INTEGER NOT NULL, dist REAL NOT NULL, via_waypoint INTEGER NOT
  NULL)
    - The parser computes a waypoint‑aware symmetric distance for all pairs of `targets` (where
      target == ports or station endpoints) and stores `via_waypoint`=1 if the shortest valid route
      used a waypoint; `via_waypoint`=0 otherwise.

How distances are computed
-------------------------

- After parsing the input lines the parser gathers all targets (locations with types `P`, `S1`,
  `S2`) and all waypoints (`W`).
- It attempts to load an island polygon from `bin/island.bin` (binary format: uint32 count, then
  `count` pairs of int32 lat,int32 lon). If the polygon is present the parser treats direct straight
  segments that intersect the polygon as invalid (i.e., `direct_valid = 0`).
- The parser computes the Euclidean distance in the stored integer coordinate space (a
  project‑specific local metric) between target pairs. If a direct line is invalid due to polygon
  crossing, the parser attempts to route via a waypoint (sum of two leg distances) and if any
  waypoint yields a smaller valid distance it selects that as `best` and sets `via_waypoint = 1`.
- If no valid route is found (direct invalid and no waypoint helps), the pair is skipped (no
  insertion into distances).
- The distances table is created fresh each run (the parser drops any existing `distances` table
  before computing the new one).

Running the parser
------------------
Build requirements:

- `sqlite3` development headers/libraries are needed to compile `parse_dat_to_sqlite.c` (the parser
  uses the SQLite C API).

Build (simple):

```bash
# from Code/src/ (is also included in make target 'all')
make -C src db
```

Run:

```bash
# from repository root
# default outdir is 'dat'
./bin/parse_dat_to_sqlite dat/data2023spring.dat
# or specify an output directory explicitly
./bin/parse_dat_to_sqlite dat/data2023spring.dat dat
# resulting DB: dat/parsed_data.sqlite
```

Inspecting the database (examples)
---------------------------------
Below are a few practical commands and small snippets you can copy/paste to inspect the resulting
`dat/parsed_data.sqlite` after running the parser.

1) sqlite3 CLI (quick checks)

- List tables:

```bash
sqlite3 dat/parsed_data.sqlite ".tables"
```

- Show the full schema or a single table schema:

```bash
sqlite3 dat/parsed_data.sqlite ".schema"
sqlite3 dat/parsed_data.sqlite ".schema stations"
```

- Show column info for a table (useful to verify renamed columns):

```bash
sqlite3 dat/parsed_data.sqlite "PRAGMA table_info(stations);"
```

- Row counts and sample rows:

```bash
sqlite3 dat/parsed_data.sqlite "SELECT 'locations', COUNT(*) FROM locations;"
sqlite3 dat/parsed_data.sqlite "SELECT 'stations', COUNT(*) FROM stations;"
sqlite3 dat/parsed_data.sqlite "SELECT id,lat,lon,type FROM locations LIMIT 10;"
sqlite3 dat/parsed_data.sqlite "SELECT id,ext_id,bottom_depth_cast,bottom_depth_haul,remark FROM stations LIMIT 10;"
```

- Check the distances table and sample entries:

```bash
sqlite3 dat/parsed_data.sqlite "SELECT COUNT(*) FROM distances;"
sqlite3 dat/parsed_data.sqlite "SELECT i,j,dist,via_waypoint FROM distances ORDER BY i,j LIMIT 10;"
```

2) Small bash summary (shows counts and first 5 rows per table)

```bash
#!/usr/bin/env bash
DB=dat/parsed_data.sqlite
sqlite3 "$DB" "SELECT 'locations', COUNT(*) FROM locations;"
sqlite3 "$DB" "SELECT 'ports', COUNT(*) FROM ports;"
sqlite3 "$DB" "SELECT 'boats', COUNT(*) FROM boats;"
sqlite3 "$DB" "SELECT 'stations', COUNT(*) FROM stations;"
sqlite3 "$DB" "SELECT 'waypoints', COUNT(*) FROM waypoints;"

echo -e "\nFirst 5 locations:";
sqlite3 -column -header "$DB" "SELECT id,lat,lon,type FROM locations LIMIT 5;"

echo -e "\nFirst 5 distances:";
sqlite3 -column -header "$DB" "SELECT i,j,dist,via_waypoint FROM distances LIMIT 5;"
```

3) GUI tools

- If you prefer a GUI, open `dat/parsed_data.sqlite` in "DB Browser for SQLite" (https://sqlitebrowser.org),
  SQLiteStudio, or another SQLite GUI — these let you browse tables, run ad-hoc queries, export CSV, and
  inspect indexes.