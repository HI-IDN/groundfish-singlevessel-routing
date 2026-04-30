# Discussion and Conclusions

## Discussion

The matheuristic acts as a strong local improvement operator. It consistently upgrades diverse initializations, yet still benefits most from a strong starting route. This explains why stronger initializations retain an absolute advantage even though weaker ones may achieve larger relative gains.

Computationally, the two-segment C-MIP delivers useful incumbents quickly, which makes short per-call time limits effective. Longer limits mainly inflate runtime.

Methodologically, the neighborhood remains local. Segment membership is only weakly adjustable, so the search can converge to a high-quality local optimum without large-scale restructuring. The clearest route to further gains is to broaden the set of admissible moves in a controlled way.

Operationally, the method produces fast, reproducible plans that can support annual survey planning and scenario analysis without requiring intractable end-to-end optimization.

## Future work

The paper suggests several natural extensions:

1. Broaden the neighborhood beyond repeated two-segment optimization.
2. Add occasional three-segment optimizations.
3. Introduce controlled station transfers across multiple boundaries.
4. Use periodic shake or resegmentation moves to improve global exploration.
5. Extend the method to multiple vessels.
6. Incorporate service times and optimize for makespan, not only distance.
7. Support heterogeneous fleets with different capacities, speeds, and operating limits.
8. Add uncertainty in realized catches, service times, and sailing times.

## Conclusions

The paper presents a matheuristic for capacitated groundfish survey planning that improves an initial segmented route through repeated, time-limited two-segment MIP refinements. Mandatory port returns and direction-dependent distances make large-scale survey planning computationally difficult. The full capacitated MIP is intractable on operationally sized instances, but the proposed method still reliably improves initial plans while keeping runtimes practical.

The experiments on the 2023 Icelandic groundfish survey show that the method consistently reduces total distance. The best solution improves the baseline by about 270 nautical miles, equivalent to roughly one day of vessel time. Increasing the two-segment time limit beyond 2 to 3 minutes gives no consistent benefit, confirming that short, repeated local solves are the right tradeoff for this setting.

Overall, restricted-MIP local improvement provides a practical and scalable way to construct high-quality, reproducible survey plans for operational use, strategic planning, and scenario comparison.

## Availability

The paper states that the implementation and the 2023 survey data are openly available at:

[`https://github.com/HI-IDN/groundfish-singlevessel-routing`](https://github.com/HI-IDN/groundfish-singlevessel-routing)
