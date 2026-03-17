# Groundfish Survey Problem

Groundfish surveys provide critical information on fish-stock abundance, distribution, and biological characteristics, and they support sustainable harvest management. In Iceland, these surveys have followed a fixed-station sampling design since 1985, with the same set of trawl stations revisited annually in spring to maintain a consistent long-term time series.

For a survey with a given set of stations, the survey leader must determine an efficient routing plan for one or more vessels. The routing problem is shaped by regulatory and operational constraints. During the survey, some catch is retained and later sold, so vessels must periodically return to designated ports to unload because onboard catch capacity is limited. In practice, unloading occurs regularly, typically within five days of operations, which embeds mandatory port calls into the routing plan.

The amount of fish caught at each station is uncertain and varies from year to year. Routing plans therefore need to be robust to variability in catch volumes while still maintaining survey coverage. When multiple vessels operate simultaneously, stations must also be partitioned across vessels in a capacity- and schedule-feasible way.

Each trawl station is defined by two tow endpoints, and feasible tow directions may be limited by bathymetry, weather, or vessel-specific operational constraints. Travel between stations is therefore directional: the distance from station A to station B need not equal the distance from B to A. Straight-line travel may also cross land, so waypoint-aware distances are computed to reroute invalid arcs through intermediate waypoints. These corrected, direction-aware distances are then used in both the exact MIP model and the matheuristic.

## Core characteristics

1. A large number of geographically distributed stations.
2. Direction-dependent travel and towing constraints.
3. Mandatory port returns due to finite vessel capacity and retained catch.
