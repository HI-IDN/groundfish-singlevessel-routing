# Introduction

Bottom trawl groundfish surveys require visiting a set of sampling stations while respecting operational constraints such as limited onboard capacity, time, and cost. Even when station locations are fixed, the order of visits and the timing of port returns directly influence survey feasibility and efficiency. A method that can generate good survey routes quickly is therefore valuable for both survey planning and survey design.

From an optimization perspective, these decisions couple routing and capacity in a way that makes exact mixed-integer programming approaches computationally challenging at realistic scales. Matheuristics address this difficulty by combining mathematical programming with heuristic search, often using MIP subproblems to improve a larger solution.

This paper introduces a scalable matheuristic for routing capacity-constrained groundfish surveys. Starting from a feasible segmented route, the method applies short, targeted MIP refinements to adjust segment boundaries under vessel capacity. Improvements are accepted whenever found, and the process repeats until no further gains arise. The result is a practical baseline planning tool for survey design and scenario evaluation.

## Roadmap

1. The groundfish survey routing problem is defined first.
2. The capacity-aware MIP model is then described.
3. The scalable matheuristic is introduced after that.
4. The paper closes with experiments, results, discussion, and conclusions.
