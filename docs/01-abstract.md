# Abstract

Bottom trawl groundfish surveys are planned over a fixed set of sampling stations, but the order of visits and the timing of port returns strongly affect total travel distance and operational feasibility. Although mixed-integer programming (MIP) models can encode capacity and port-call constraints directly, solving the full model to optimality is often impractical at realistic scales. This paper proposes a matheuristic that combines fast tour construction with repeated calls to a time-limited capacity MIP subproblem.

Starting from an initial segmented tour, the method repeatedly examines boundaries between adjacent segments separated by a port visit and solves a restricted two-segment capacity MIP on the union of stations in the two segments, with fixed endpoints and a single intermediate port call. The MIP reallocates stations between the two segments subject to capacity, and the best incumbent found within the time limit is accepted if it improves total distance.

The approach is evaluated under four initialization strategies and a range of MIP time limits. Across scenarios, the method consistently improves baseline plans and provides a fast, reproducible tool for strategic planning and scenario comparison. The results indicate that restricted-MIP local improvement is an effective mechanism for producing high-quality survey routes under realistic operational constraints.

Keywords: Groundfish Survey Problem, Multi-trip VRP, Capacitated Routing with Replenishment, Segment-Based Decomposition, Matheuristics
