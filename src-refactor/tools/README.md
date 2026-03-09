# GSP Tools

Utility tools for the GSP Solver project.

## export_survey_json

Export survey_2023 data from the database to JSON format compatible with the plotting scripts.

### Usage

```bash
export_survey_json <database.db> <output_prefix> [boat_id]
```

**Arguments:**
- `database.db` - Path to the SQLite database containing survey_2023 data
- `output_prefix` - Output file prefix (e.g., `sol/survey`)
- `boat_id` - (Optional) Specific boat ID to export, or 0/omit to export all boats

**Output:**
- When boat_id is specified: `<output_prefix>_boat<id>.json`
- When boat_id is 0 or omitted: `<output_prefix>_boat1.json`, `<output_prefix>_boat2.json`, etc.

### Examples

```bash
# Export all boats from survey_2023
./export_survey_json dat/gsp_data.db sol/survey

# Export only boat 2
./export_survey_json dat/gsp_data.db sol/survey 2
```

### Output Format

The JSON output is compatible with `py/plot_solution.py` and includes:

- **metadata**: Boat ID, name, home port location, timestamp
- **problem**: Number of nodes, stations, capacity
- **solution**: Tour sequence, total distance in nautical miles
- **solver_stats**: Export method and status

### Makefile Integration

```bash
# From src-refactor/ directory
make export_survey       # Exports all boats to sol/survey_boat*.json
```

## Notes

- Only **selected** ports and stations from survey_2023 are exported
- Waypoints are **not** included in the survey route (they're routing helpers only)
- Distance calculations use the precomputed distance matrix with Dijkstra waypoint routing
- Each boat's route includes its home port location for accurate plotting
- Multi-boat exports allow visualization of fleet-wide survey coverage

