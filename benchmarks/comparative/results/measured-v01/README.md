# Comparative performance (measured)

Boundary: propagation plus generation/retention of requested output. All rows were accuracy-checked at their actual duration/cadence before timing. Setup, imports and whole-process costs are separate; no reference generation is inside headline clocks.

| Scenario / simulated seconds | LEO C++ ms | SciPy DOP853 ms | Native Tudat ms | SciPy / LEO | Tudat / LEO |
|---|---:|---:|---:|---:|---:|
| coupled_case_a / 600 (cadence 10 s) | 2.855 | 398.337 | Unmatched | 139.53× | — |
| coupled_case_a / 21600 (cadence 30 s) | 67.922 | 12065.808 | Unmatched | 177.64× | — |
| coupled_case_b / 600 (cadence 10 s) | 1.406 | 437.912 | Unmatched | 311.41× | — |
| coupled_case_b / 21600 (cadence 30 s) | 49.269 | 12229.452 | Unmatched | 248.22× | — |
| drag_flat_plate / 600 (cadence 10 s) | 0.992 | 363.735 | Unmatched | 366.48× | — |
| drag_flat_plate / 3600 (cadence 10 s) | 5.921 | 2545.358 | Unmatched | 429.87× | — |
| j2_inclined / 600 (cadence 10 s) | 0.143 | 151.877 | 5.463 | 1059.29× | 38.10× |
| j2_inclined / 21600 (cadence 30 s) | 4.602 | 3670.094 | 102.547 | 797.46× | 22.28× |
| torque_free_elongated / 600 (cadence 10 s) | 0.132 | 94.410 | 1.713 | 714.75× | 12.97× |
| torque_free_elongated / 1800 (cadence 5 s) | 0.422 | 278.929 | 5.980 | 660.24× | 14.15× |
| two_body_circular / 600 (cadence 10 s) | 0.114 | 94.933 | 1.736 | 834.88× | 15.27× |
| two_body_circular / 21600 (cadence 30 s) | 4.399 | 4062.996 | 98.699 | 923.65× | 22.44× |

Ratios compare equivalent equations, not identical nominal tolerances. Values below 1× mean the named Python tool is faster than LEO; there is no arbitrary performance acceptance gate.

## Sequential ensembles

| Scenario | Tool | Trajectories | Median s | Min–max s | Trajectories/s |
|---|---|---:|---:|---|---:|
| j2_inclined | cpp | 8 | 0.0010 | 0.0009–0.0010 | 8006.7 |
| j2_inclined | scipy | 8 | 0.7177 | 0.7160–1.0045 | 11.1 |
| j2_inclined | tudat | 8 | 0.0249 | 0.0245–0.0266 | 321.8 |
| j2_inclined | cpp | 64 | 0.0083 | 0.0081–0.0085 | 7704.1 |
| j2_inclined | scipy | 64 | 5.6623 | 5.5223–5.8465 | 11.3 |
| j2_inclined | tudat | 64 | 0.2169 | 0.2066–0.2401 | 295.1 |
| coupled_case_a | cpp | 8 | 0.0151 | 0.0147–0.0154 | 530.7 |
| coupled_case_a | scipy | 8 | 2.8885 | 2.8526–3.0203 | 2.8 |
| coupled_case_a | cpp | 64 | 0.1225 | 0.1190–0.1245 | 522.3 |
| coupled_case_a | scipy | 64 | 23.8679 | 23.4974–23.9094 | 2.7 |

## Native strong scaling

See `strong-scaling.svg`: actual 1/2/4/8 native worker counts, normalised to the same 64-member inline batch. This matched constant-Sun environment does not acquire SPICE and is not a replacement for MT-002’s real-environment contention measurements.

## Setup, imports and output

| Scenario / tool (single, longest window) | Setup ms | Import ms | Whole fresh process s (warmup + all repeats + publication) |
|---|---:|---:|---:|
| coupled_case_a / cpp | 1.305 | — | 0.283 |
| coupled_case_a / scipy | 0.636 | 463.0 | 49.169 |
| coupled_case_b / cpp | 1.652 | — | 0.206 |
| coupled_case_b / scipy | 0.810 | 533.0 | 50.360 |
| drag_flat_plate / cpp | 0.770 | — | 0.028 |
| drag_flat_plate / scipy | 0.495 | 509.7 | 10.607 |
| j2_inclined / cpp | 1.631 | — | 0.025 |
| j2_inclined / scipy | 0.606 | 434.7 | 15.599 |
| j2_inclined / tudat | 0.412 | 6039.6 | 6.525 |
| torque_free_elongated / cpp | 0.919 | — | 0.007 |
| torque_free_elongated / scipy | 0.462 | 489.1 | 1.716 |
| torque_free_elongated / tudat | 0.326 | 5424.4 | 5.506 |
| two_body_circular / cpp | 1.333 | — | 0.024 |
| two_body_circular / scipy | 0.605 | 439.7 | 16.927 |
| two_body_circular / tudat | 0.853 | 7670.0 | 8.209 |

The whole-process value is independently measured around a fresh child, not a per-trajectory solver median and not an inferred interpreter-startup estimate. Python workers retain only requested output in ordinary timing. Native Tudat body construction is outside its solver clock and explicitly included in repeated-setup/whole-process data.

## Counters and memory

Raw rows retain RHS evaluation counts, all repetitions, achieved position/velocity/SO(3)/rate errors, setup/solver/sampling times and child command lines. Python-level RHS latencies are separate diagnostics; they are not subtracted from independently measured medians. SciPy DOP853 uses Python stage/control logic with compiled NumPy algebra, not an independently isolatable compiled DOP853 engine.

Python memory values are reliable fresh-process peak RSS including interpreter, imports and setup, not incremental trajectory memory. Logical retained float64 output bytes are separate. No comparable C++ RSS or total-memory speedup is inferred.

## Unmatched models

Native Tudat environmental panel/thermal/atmosphere/albedo equations differ from the project’s flat-plate/cold-beam/radial-beam definitions. Such rows are marked `UNMATCHED`; a default Tudat atmosphere is never presented as equivalent. SciPy covers those matched equations.
