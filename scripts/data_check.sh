#!/bin/bash

grep "^WAYP" dat/data2023spring.dat > waypoints.old
sqlite3 -separator " " dat/gsp_data.db "
SELECT
 'WAYP' as type,
 l.easting,
 l.northing,
 '1' as is_waypoint
FROM waypoints w
INNER JOIN locations l ON w.location_id = l.id
ORDER BY l.id;
" > waypoints.new
# manual comparison matches exactly
rm waypoints.old waypoints.new

grep "^PORT" dat/data2023spring.dat > ports.old
sqlite3 -separator " " dat/gsp_data.db "
SELECT
 'PORT' as type,
 l.easting,
 l.northing,
 '\"'||p.name||'\"' as name,
 case when p.selected = 0 then '0 #' else p.selected end as is_selected
FROM ports p
INNER JOIN locations l ON p.location_id = l.id
ORDER BY p.id;
" > ports.new
# manual comparison does NOT match

grep "^BOAT" dat/data2023spring.dat > boats.old
sqlite3 -separator " " dat/gsp_data.db "
SELECT
  'BOAT' as type,
  l1.easting, l1.northing,
  l2.easting, l2.northing,
  b.capacity,
  b.c1,
  b.c2,
  b.c3,
  b.c4,
  b.c5,
  b.c6,
  '\"'||b.name||'\"' as name
FROM boats b
INNER JOIN locations l1 ON b.start_location_id = l1.id
INNER JOIN locations l2 ON b.end_location_id = l2.id
ORDER BY b.id;
" > boats.new
head boats.old boats.new
# Missing 6 boats columns, and 2 locations columns are OFF


grep "^STAT" dat/data2023spring.dat > stations.old
sqlite3 -header -separator " " dat/gsp_data.db "
SELECT
  'STAT' as type,
  s.id,
  '?',
  '?',
  l1.easting, l1.northing,
  l2.easting, l2.northing,
  '?',
  '?',
  ' #   '|| s.comment ||'\\\ botndypi_kastad= '||s.depth_thrown||' botndypi_hift= '||s.depth_haul||' \\\' as comment
FROM stations s
INNER JOIN locations l1 ON s.start_location_id = l1.id
INNER JOIN locations l2 ON s.end_location_id = l2.id
ORDER BY s.id;
" > stations.new
head stations.old stations.new