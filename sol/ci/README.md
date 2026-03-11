# Cheapest‑Insertion (CI) Initialization

The Cheapest‑Insertion (CI) initialization constructs an initial ordering by iteratively inserting
the unvisited station whose inclusion causes the smallest marginal increase in the total tour
length. At each iteration, the algorithm evaluates all possible insertion positions in the current
partial tour and selects the station–position pair that yields the minimal additional travel
distance.

Compared with nearest‑neighbor, CI invests slightly more computation per step in order to build
smoother, more globally consistent orderings while remaining lightweight and scalable. When a
prospective insertion would violate vessel capacity in the current segment, the algorithm inserts a
port, resets accumulated load, and then continues inserting stations using the same
marginal‑increase rule.

Overall, CI strikes a balance between computational efficiency and route quality: it tends to
produce higher‑quality initial orderings than NN while maintaining low overhead, making it a strong
default initialization for downstream refinement.

![CI Initialization](init.png)