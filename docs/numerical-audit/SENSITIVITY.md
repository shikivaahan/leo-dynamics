# Physical sensitivity results — v1.1

Measured after [EXPERIMENT_DESIGN.md](EXPERIMENT_DESIGN.md) and the documented
[analysis correction amendment](experiments/ANALYSIS_AMENDMENT-v1.1.md).
Evidence: `evidence/sensitivity-results.json.gz`; regenerate with
`run_experiments.sh` (a fresh output directory is required).

## Interpretation contract

A resolved response is at least 10 times its same-unit empirical numerical
envelope and has at most 20% cross-method and neighboring-size derivative
disagreement. The envelope includes nominal AND both perturbed trajectories,
independent adaptive DP5/RKF78, identical-schedule RK4 refinement, and nominal
mixed-state exposure. Sampled scalar derivatives additionally pass actual
15 s cadence checks of BOTH perturbed trajectories on the identical RK4 grid.
This is an empirical floor, not a certified bound.
No finite difference from an unresolved cell is interpreted as a physical derivative.

Dimensional derivatives for uniform mass, area, inertia and spin scaling are
converted to kg, physical surface area (CASE-B membrane 9 m²), inertia trace
at fixed ratios, and initial rate magnitude at fixed direction. They are not
independent derivatives of individual tensor entries or individual panels.
Elasticities are undefined for a zero/arbitrary parameter origin, vector-valued
state observable, or a baseline not separated from zero by 10U. Exploratory
spans are not mission uncertainty distributions; OFAT inputs are independent
model changes, not complete redesigns obeying hardware parameter correlations.

## Numerical envelopes and prediction horizons

State envelopes are maximum common-time discrepancies over the horizon; scalar
envelopes are spreads of the specified terminal or integrated observable.
Cadence is included for sampled averages/work, not inferred from an interpolant.

| Case | Horizon (s) | Position U (m) | SO(3) U (rad) | Rate U (rad/s) | SMA U (m) | Mean-area U (m²) | SRP-work U (J) |
|---|---:|---:|---:|---:|---:|---:|---:|
| CASE-A | 60 | 6.3336e-08 | 1.1632e-11 | 6.2535e-13 | 1.4342e-07 | 0.001629 | 0.0023923 |
| CASE-A | 600 | 1.0376e-07 | 1.9258e-10 | 1.5246e-12 | 1.9744e-07 | 4.3047e-05 | 0.0015291 |
| CASE-A | 1800 | 4.1349e-07 | 1.067e-09 | 1.7321e-12 | 5.5693e-07 | 2.3852e-05 | 0.0070198 |
| CASE-A | 3600 | 2.5417e-06 | 2.6253e-09 | 1.7321e-12 | 9.7975e-07 | 2.4942e-05 | 0.016822 |
| CASE-A | 7200 | 9.6117e-06 | 5.4422e-09 | 3.757e-12 | 1.1679e-06 | 9.5288e-07 | 0.015686 |
| CASE-A | 10800 | 1.5682e-05 | 6.6258e-09 | 3.7793e-12 | 1.3523e-06 | 1.8846e-06 | 0.020987 |
| CASE-A | 21600 | 3.9664e-05 | 1.8238e-08 | 9.0634e-12 | 1.7602e-06 | 8.5817e-08 | 0.035867 |
| CASE-B | 60 | 2.0495e-08 | 3.9347e-15 | 6.7822e-18 | 7.6368e-08 | 0.0015612 | 0.0010758 |
| CASE-B | 600 | 7.977e-08 | 2.8983e-14 | 7.7817e-17 | 9.3132e-08 | 0.0015463 | 0.015584 |
| CASE-B | 1800 | 4.0001e-07 | 7.3966e-09 | 1.1237e-11 | 3.634e-06 | 0.0016649 | 7.1668 |
| CASE-B | 3600 | 9.859e-06 | 1.0396e-08 | 1.1237e-11 | 3.6433e-06 | 0.0010543 | 7.1668 |
| CASE-B | 7200 | 3.7223e-05 | 7.7903e-08 | 1.0639e-10 | 4.217e-06 | 0.00016205 | 12.251 |
| CASE-B | 10800 | 5.9111e-05 | 8.2187e-08 | 1.0639e-10 | 6.2305e-06 | 4.8086e-05 | 7.9762 |
| CASE-B | 21600 | 0.00013055 | 2.7268e-05 | 2.7103e-08 | 9.5833e-06 | 9.7862e-05 | 9.8737 |
| CASE-C | 60 | 4.098e-08 | 7.697e-11 | 1.3669e-12 | 1.5274e-07 | 0.0016783 | 0.0038483 |
| CASE-C | 600 | 4.8679e-07 | 2.098e-07 | 1.2419e-09 | 3.6601e-06 | 0.0053074 | 1.0201 |
| CASE-C | 1800 | 4.7385e-06 | 3.2059e-07 | 1.2419e-09 | 1.12e-05 | 0.0018449 | 1.0201 |
| CASE-C | 3600 | 4.4167e-05 | 8.4768e-07 | 4.253e-09 | 1.6918e-05 | 0.00068991 | 2.5976 |
| CASE-C | 7200 | 0.00032355 | 0.00021001 | 7.6637e-07 | 0.0011594 | 0.0015347 | 4.0885 |
| CASE-C | 10800 | 0.036063 | 0.0040936 | 1.3414e-05 | 0.0094768 | 0.0017457 | 2.8795 |
| CASE-C | 21600 | 19.004 | 3.9708 | 0.01293 | 6.6553 | 0.020705 | 13.485 |

For the predeclared **conditional** 0.1 m orbit and 0.01° orientation
integration allowances, CASE-A and CASE-B support six hours; CASE-C supports
three hours for orbit and one hour for instantaneous orientation among the
tested horizons. These are not flight requirements or certified global bounds.
CASE-C's formal two-times SO(3) spread may exceed π; it is an uncertainty
margin, not an orientation distance (all actual distances are ≤π).
CASE-B's nominal six-hour orientation is resolved at coarse planning accuracy
but several perturbed six-hour trajectories/derivatives are not.

## Resolved finite differences

Each table separates observables and horizons. The largest resolved tested
span is shown for each parameter, with all resolved sizes listed. Response R
and envelope U share the stated observable units. Signed scalar derivatives
and local relative-rotation-vector norms are distinguished; vector state
derivatives are not derivatives of a wrapped angle component.

### CASE-A, 3600 s — 308/882 resolved cells

**sma_final_m [m]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 0.1, 1, 10 | 20.198 | 9.7975e-07 | 2.0198 m / m | 2.0188 | 7.1009e-09 / 3.7026e-08 | 2.4324e-06 |
| velocity_along | 0.0001, 0.001, 0.01 | 19.42 | 9.7975e-07 | 1941.9 m / m/s | 2.019 | 1.2205e-08 / 9.0809e-08 | 6.2068e-06 |
| body_rate | 1e-05, 0.0001 | 0.00011062 | 9.7975e-07 | -1.104 m / rad/s | -3.0775e-09 | 0.0020415 / 0.10486 | 0.0040999 |
| density | 0.01, 0.05, 0.1 | 0.00092026 | 9.7975e-07 | -0.0091991 m / 1 | -1.2822e-09 | 0.00022369 / 0.00018827 | 0.00075525 |
| area | 0.01, 0.05, 0.1 | 0.0016724 | 9.7975e-07 | -0.076015 m / m² | -2.3309e-09 | 9.1331e-05 / 0.00024504 | 8.799e-05 |
| mass | 0.01, 0.05, 0.1 | 0.0018584 | 9.7975e-07 | 0.001689 m / kg | 2.3541e-09 | 0.00014142 / 0.0074126 | 0.20057 |
| accommodation | 0.01, 0.03, 0.05 | 0.00010783 | 9.7975e-07 | 0.0021529 m / 1 | 2.7007e-10 | 0.00075688 / 0.0013479 | 0.0034521 |
| specular | 0.01, 0.03, 0.05 | 0.00010624 | 9.7975e-07 | 0.0021185 m / 1 | 8.8583e-11 | 0.0014947 / 0.0028883 | 0.0060756 |
| diffuse | 0.01, 0.03, 0.05 | 0.00011987 | 9.7975e-07 | -0.0023921 m / 1 | -6.6684e-11 | 0.00056842 / 0.0025139 | 0.0044773 |

**mean_projected_area_m2 [m²]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| area | 0.01, 0.05, 0.1 | 0.0056036 | 2.7441e-05 | 0.25471 m² / m² | 1 | 0.00022288 / 1.1583e-10 | 3.4119e-07 |

**attitude_angle_rad [rad]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 10 | 4.2412e-08 | 2.6253e-09 | 4.2276e-09 rad / m | undefined | 0.0012207 / 5.2551e-05 | undefined |
| attitude_orbit | 1e-06, 1e-05, 0.0001 | 0.00011195 | 2.6253e-09 | 1.1195 rad / rad | undefined | 1.1707e-06 / 3.0531e-08 | undefined |
| body_rate | 1e-06, 1e-05, 0.0001 | 0.36034 | 5.0025e-09 | 3603.2 rad / rad/s | undefined | 2.2426e-09 / 7.3197e-05 | undefined |
| density | 0.01, 0.05, 0.1 | 1.5955e-06 | 2.6253e-09 | 1.5928e-05 rad / 1 | undefined | 7.2211e-06 / 1.9524e-07 | undefined |
| area | 0.01, 0.05, 0.1 | 4.2636e-05 | 2.6253e-09 | 0.001938 rad / m² | undefined | 1.2791e-06 / 1.1781e-08 | undefined |
| inertia | 0.01, 0.05, 0.1 | 4.7374e-05 | 2.6253e-09 | 0.0018459 rad / kg·m² (trace, fixed ratios) | undefined | 1.4629e-06 / 0.0075204 | undefined |
| cp_y | 0.0001, 0.001, 0.005 | 0.0021329 | 7.7297e-09 | 0.42636 rad / m | undefined | 1.4708e-06 / 5.9021e-07 | undefined |
| accommodation | 0.03, 0.05 | 9.2629e-08 | 2.6253e-09 | 1.8503e-06 rad / 1 | undefined | 9.5845e-05 / 7.2577e-06 | undefined |
| specular | 0.01, 0.03, 0.05 | 1.051e-05 | 2.6253e-09 | 0.00021019 rad / 1 | undefined | 8.931e-06 / 2.5402e-08 | undefined |
| diffuse | 0.01, 0.03, 0.05 | 5.8791e-06 | 2.6253e-09 | 0.00011758 rad / 1 | undefined | 7.2555e-06 / 2.6758e-08 | undefined |

### CASE-A, 21600 s — 320/882 resolved cells

**sma_final_m [m]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 0.1, 1, 10 | 20.883 | 1.7602e-06 | 2.0883 m / m | 2.0869 | 9.4099e-09 / 1.5408e-08 | 4.1292e-06 |
| velocity_along | 0.0001, 0.001, 0.01 | 20.068 | 1.7602e-06 | 2006.8 m / m/s | 2.0861 | 1.0976e-08 / 8.6553e-09 | 6.7344e-07 |
| body_rate | 1e-05 | 9.4455e-05 | 2.0489e-06 | 8.0435 m / rad/s | 2.2419e-08 | 0.0068446 / 0.018786 | 0.3486 |
| density | 0.01, 0.05, 0.1 | 0.0056229 | 1.7602e-06 | -0.056221 m / 1 | -7.8348e-09 | 4.6466e-05 / 3.6361e-05 | 0.00029105 |
| area | 0.01, 0.05, 0.1 | 0.005849 | 2.5779e-06 | -0.26586 m / m² | -8.1508e-09 | 0.00014297 / 4.0604e-06 | 5.0477e-05 |
| mass | 0.01, 0.05, 0.1 | 0.0065001 | 1.7602e-06 | 0.0059081 m / kg | 8.2334e-09 | 8.3855e-05 / 0.0075893 | 0.20039 |
| accommodation | 0.01, 0.03, 0.05 | 0.00068092 | 2.4177e-06 | 0.013602 m / 1 | 1.706e-09 | 0.00097736 / 0.00079819 | 0.0024499 |
| diffuse | 0.03, 0.05 | 4.8851e-05 | 2.1383e-06 | -0.00095891 m / 1 | -2.6726e-11 | 0.015857 / 0.0063501 | 0.037762 |

**mean_projected_area_m2 [m²]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| area | 0.01, 0.05, 0.1 | 0.0056086 | 1.1021e-07 | 0.25493 m² / m² | 1 | 2.0463e-06 / 8.3951e-09 | 1.0355e-06 |

**attitude_angle_rad [rad]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 10 | 1.2741e-06 | 1.8238e-08 | 1.2565e-07 rad / m | undefined | 0.0014158 / 0.00010766 | undefined |
| velocity_along | 0.01 | 1.3458e-06 | 1.8238e-08 | 0.00013285 rad / m/s | undefined | 0.0015377 / 8.3509e-05 | undefined |
| attitude_orbit | 1e-06, 1e-05, 0.0001 | 0.00017096 | 1.8238e-08 | 1.7096 rad / rad | undefined | 1.138e-05 / 1.8716e-07 | undefined |
| body_rate | 1e-06, 1e-05, 0.0001 | 2.1599 | 2.6909e-08 | 21596 rad / rad/s | undefined | 3.0492e-09 / 0.00096059 | undefined |
| density | 0.01, 0.05, 0.1 | 9.7449e-06 | 1.8238e-08 | 9.7436e-05 rad / 1 | undefined | 5.195e-05 / 4.5839e-07 | undefined |
| area | 0.01, 0.05, 0.1 | 0.00037964 | 1.8238e-08 | 0.017255 rad / m² | undefined | 7.6021e-06 / 3.5841e-07 | undefined |
| inertia | 0.01, 0.05, 0.1 | 0.00042178 | 1.8238e-08 | 0.016435 rad / kg·m² (trace, fixed ratios) | undefined | 5.0068e-06 / 0.0075111 | undefined |
| cp_y | 0.0001, 0.001, 0.005 | 0.019027 | 9.1707e-08 | 3.7962 rad / m | undefined | 1.3645e-06 / 1.2975e-05 | undefined |
| accommodation | 0.03, 0.05 | 3.5227e-07 | 1.8238e-08 | 6.9987e-06 rad / 1 | undefined | 0.00041209 / 2.099e-05 | undefined |
| specular | 0.01, 0.03, 0.05 | 9.5317e-05 | 1.8238e-08 | 0.0019063 rad / 1 | undefined | 3.4295e-05 / 6.6036e-08 | undefined |
| diffuse | 0.01, 0.03, 0.05 | 5.156e-05 | 1.8238e-08 | 0.0010312 rad / 1 | undefined | 2.7e-05 / 1.2927e-07 | undefined |

### CASE-B, 3600 s — 397/882 resolved cells

**sma_final_m [m]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 0.1, 1, 10 | 20.318 | 3.6433e-06 | 2.0318 m / m | 2.0314 | 2.3216e-08 / 9.6028e-09 | 2.2658e-06 |
| velocity_along | 0.0001, 0.001, 0.01 | 19.551 | 3.6433e-06 | 1955.1 m / m/s | 2.0333 | 1.6958e-08 / 4.8635e-08 | 5.9449e-06 |
| attitude_orbit | 1e-05, 0.0001 | 0.0012279 | 3.6433e-06 | 12.274 m / rad | undefined | 0.00015593 / 0.00012746 | 0.00076787 |
| body_rate | 1e-06, 1e-05, 0.0001 | 0.24349 | 3.6433e-06 | -1316.7 m / rad/s | undefined | 2.6559e-06 / 0.023323 | 1.6984 |
| density | 0.01, 0.05, 0.1 | 0.18832 | 3.6433e-06 | -1.8827 m / 1 | -2.6249e-07 | 1.2441e-06 / 2.0059e-06 | 0.00057019 |
| area | 0.01, 0.05, 0.1 | 0.40139 | 3.6433e-06 | 0.44478 m / m² | 5.5811e-07 | 5.13e-07 / 1.6867e-06 | 0.0054195 |
| mass | 0.01, 0.05, 0.1 | 0.43271 | 3.6433e-06 | -0.39337 m / kg | -5.4845e-07 | 7.1263e-07 / 0.0075194 | 0.2 |
| inertia | 0.01, 0.05, 0.1 | 0.012071 | 3.6433e-06 | -0.0073173 m / kg·m² (trace, fixed ratios) | -1.5303e-08 | 2.2401e-05 / 0.0074897 | 0.19949 |
| cp_y | 0.0001, 0.001, 0.005 | 0.055833 | 3.6433e-06 | 10.998 m / m | undefined | 4.014e-06 / 0.00036624 | 0.030746 |
| accommodation | 0.01, 0.03, 0.05 | 0.0014368 | 3.6433e-06 | -0.02836 m / 1 | -3.5587e-09 | 0.00010704 / 0.00020487 | 0.02655 |
| specular | 0.01, 0.03, 0.05 | 0.0060519 | 3.6433e-06 | 0.091424 m / 1 | 7.648e-09 | 3.7997e-05 / 0.00039017 | 0.64785 |
| diffuse | 0.01, 0.03, 0.05 | 0.10315 | 3.6433e-06 | 2.063 m / 1 | 4.3144e-08 | 2.1624e-06 / 4.6784e-06 | 4.0179e-06 |

**mean_projected_area_m2 [m²]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| body_rate | 0.0001 | 0.068137 | 0.0010543 | -391.22 m² / rad/s | undefined | 0.006041 / 0.030265 | 1.4833 |
| area | 0.01, 0.05, 0.1 | 0.54034 | 0.0011695 | 0.60027 m² / m² | 1.0024 | 0.00012906 / 4.8691e-06 | 0.00036051 |

**attitude_angle_rad [rad]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 1, 10 | 1.0123e-05 | 1.0396e-08 | 2.3468e-07 rad / m | undefined | 0.00065757 / 7.3581e-06 | undefined |
| velocity_along | 0.0001, 0.001, 0.01 | 1.3707e-05 | 1.0396e-08 | 0.0013697 rad / m/s | undefined | 8.2693e-05 / 3.321e-06 | undefined |
| attitude_orbit | 1e-06, 1e-05, 0.0001 | 0.00030942 | 1.0396e-08 | 3.0941 rad / rad | undefined | 1.8682e-06 / 1.9705e-06 | undefined |
| body_rate | 1e-06, 1e-05, 0.0001 | 0.29616 | 1.0396e-08 | 1992.2 rad / rad/s | undefined | 1.4731e-08 / 0.018719 | undefined |
| density | 0.01, 0.05, 0.1 | 0.0038713 | 1.0396e-08 | 0.036178 rad / 1 | undefined | 1.4077e-07 / 2.8054e-06 | undefined |
| area | 0.01, 0.05, 0.1 | 0.0068749 | 1.0396e-08 | 0.0076099 rad / m² | undefined | 9.4755e-08 / 2.397e-06 | undefined |
| mass | 0.05, 0.1 | 4.0857e-07 | 1.0396e-08 | 3.7142e-07 rad / kg | undefined | 1.0576e-05 / 0.00752 | undefined |
| inertia | 0.01, 0.05, 0.1 | 0.0076417 | 1.0396e-08 | 0.0046154 rad / kg·m² (trace, fixed ratios) | undefined | 1.2057e-07 / 0.0081116 | undefined |
| cp_y | 0.0001, 0.001, 0.005 | 0.039368 | 2.9147e-08 | 7.2951 rad / m | undefined | 3.6245e-07 / 0.0027178 | undefined |
| accommodation | 0.01, 0.03, 0.05 | 0.0021491 | 1.0396e-08 | 0.040198 rad / 1 | undefined | 2.3655e-07 / 7.3042e-07 | undefined |
| specular | 0.01, 0.03, 0.05 | 0.0090229 | 1.0396e-08 | 0.096823 rad / 1 | undefined | 7.1196e-08 / 4.9625e-05 | undefined |

### CASE-B, 21600 s — 190/882 resolved cells

**sma_final_m [m]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 0.1, 1, 10 | 21.356 | 9.5833e-06 | 2.1356 m / m | 2.1345 | 1.0985e-07 / 6.803e-08 | 6.8703e-06 |
| velocity_along | 0.0001, 0.001, 0.01 | 20.53 | 9.5833e-06 | 2053 m / m/s | 2.1344 | 1.1246e-07 / 4.7338e-08 | 5.4237e-06 |
| attitude_orbit | 1e-06, 1e-05, 0.0001 | 0.046074 | 9.5833e-06 | 395.21 m / rad | undefined | 1.6213e-05 / 0.0019366 | 0.33161 |
| area | 0.01, 0.05, 0.1 | 8.6197 | 9.5833e-06 | -8.574 m / m² | -1.0755e-05 | 1.6462e-07 / 0.096258 | 0.23407 |
| mass | 0.01, 0.05, 0.1 | 1.8085 | 9.5833e-06 | 1.6441 m / kg | 2.2915e-06 | 2.1016e-07 / 0.0075205 | 0.20003 |
| inertia | 0.01, 0.05 | 5.6535 | 9.5833e-06 | 3.9923 m / kg·m² (trace, fixed ratios) | 8.3466e-06 | 3.2348e-07 / 0.0031619 | 1.7763 |
| cp_y | 0.0001, 0.001 | 5.5234 | 9.5833e-06 | -4426.6 m / m | undefined | 1.5916e-07 / 0.16355 | 0.49558 |
| accommodation | 0.01, 0.03 | 1.475 | 9.5833e-06 | -18.594 m / 1 | -2.3325e-06 | 3.0244e-06 / 0.0015269 | 3.2885 |
| diffuse | 0.01, 0.03, 0.05 | 0.067706 | 9.5833e-06 | -1.3541 m / 1 | -2.831e-08 | 7.3042e-06 / 1.7185e-05 | 1.8199e-05 |

**mean_projected_area_m2 [m²]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 10 | 0.0013342 | 0.00010038 | 0.00013281 m² / m | 203.23 | 0.0020587 / 8.8317e-05 | 0.0091908 |
| attitude_orbit | 1e-05, 0.0001 | 0.027816 | 9.7862e-05 | 260.1 m² / rad | undefined | 0.00082144 / 0.0067532 | 0.13888 |
| density | 0.1 | 0.53614 | 0.00036903 | -3.2591 m² / 1 | -0.69548 | 0.00033086 / 0.078472 | 1.2901 |

**attitude_angle_rad [rad]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 1, 10 | 0.0098397 | 2.7268e-05 | 0.00098216 rad / m | undefined | 0.00037551 / 3.8062e-05 | undefined |
| attitude_orbit | 1e-06, 1e-05, 0.0001 | 0.19513 | 2.7268e-05 | 1867.5 rad / rad | undefined | 1.098e-05 / 0.0013509 | undefined |

### CASE-C, 3600 s — 417/882 resolved cells

**sma_final_m [m]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 0.1, 1, 10 | 20.256 | 2.009e-05 | 2.0256 m / m | 2.0271 | 1.1749e-07 / 1.2674e-07 | 1.8316e-06 |
| velocity_along | 0.0001, 0.001, 0.01 | 17.901 | 1.6918e-05 | 1790.1 m / m/s | 2.0268 | 2.6523e-07 / 1.8945e-07 | 1.6609e-06 |
| attitude_orbit | 0.0001 | 0.0016408 | 2.908e-05 | -16.327 m / rad | undefined | 0.0063157 / 0.00024123 | 0.0099299 |
| body_rate | 1e-06, 1e-05, 0.0001 | 0.4127 | 1.6918e-05 | -3981.5 m / rad/s | -1.1757e-06 | 9.1296e-06 / 0.021033 | 0.07308 |
| density | 0.01, 0.05, 0.1 | 33.504 | 1.8371e-05 | -329.62 m / 1 | -4.8666e-05 | 2.5878e-07 / 0.0097954 | 0.032887 |
| area | 0.01, 0.05, 0.1 | 33.408 | 3.9537e-05 | -27.423 m / m² | -4.8584e-05 | 1.8672e-07 / 0.0098343 | 0.030431 |
| mass | 0.01, 0.05, 0.1 | 35.643 | 1.7613e-05 | 32.396 m / kg | 4.783e-05 | 1.2465e-07 / 0.0075544 | 0.20047 |
| inertia | 0.01, 0.05 | 0.69824 | 2.4294e-05 | 1.3077 m / kg·m² (trace, fixed ratios) | 1.7376e-06 | 1.2013e-05 / 0.10197 | 0.37307 |
| cp_y | 0.0001, 0.001, 0.005 | 0.66556 | 3.6743e-05 | -115.54 m / m | -1.7058e-06 | 1.0118e-05 / 0.11791 | 0.30418 |
| accommodation | 0.01, 0.03, 0.05 | 0.096792 | 4.1448e-05 | -1.8797 m / 1 | -2.4977e-07 | 6.9176e-05 / 0.01778 | 0.059714 |
| specular | 0.01, 0.03, 0.05 | 0.040759 | 1.6918e-05 | 0.81253 m / 1 | 3.5989e-08 | 6.4071e-05 / 1.2467e-05 | 0.0065424 |
| diffuse | 0.01, 0.03, 0.05 | 0.011822 | 2.6736e-05 | 0.23514 m / 1 | 6.9432e-09 | 0.00041487 / 0.00023515 | 0.011095 |

**mean_projected_area_m2 [m²]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| density | 0.05, 0.1 | 0.038878 | 0.00079317 | 0.20668 m² / 1 | 0.067181 | 0.0098129 / 0.12057 | 1.7622 |
| area | 0.01, 0.05, 0.1 | 0.34751 | 0.0015961 | 0.2743 m² / m² | 1.0699 | 0.0011819 / 0.0035818 | 0.11149 |
| inertia | 0.05, 0.1 | 0.039514 | 0.00068991 | -0.024001 m² / kg·m² (trace, fixed ratios) | -0.070213 | 0.0041861 / 0.079928 | 1.6586 |
| cp_y | 0.005 | 0.018955 | 0.0010106 | 2.1162 m² / m | 0.068788 | 0.043801 / 0.089426 | 1.5829 |
| spin | 0.05 | 0.038405 | 0.0022054 | -109.15 m² / rad/s (fixed direction) | -0.081292 | 0.080371 / 0.16241 | 4.1426 |

**attitude_angle_rad [rad]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| position_radial | 0.1, 1, 10 | 0.0054173 | 3.4094e-06 | 0.00052265 rad / m | undefined | 8.9647e-05 / 0.00023581 | undefined |
| velocity_along | 0.0001, 0.001, 0.01 | 0.00575 | 1.3793e-06 | 0.55332 rad / m/s | undefined | 9.6388e-05 / 7.3031e-05 | undefined |
| attitude_orbit | 1e-06, 1e-05, 0.0001 | 0.0022169 | 2.4523e-06 | 22.15 rad / rad | undefined | 0.00029358 / 0.00010419 | undefined |
| body_rate | 1e-06, 1e-05, 0.0001 | 0.13369 | 4.4358e-06 | 1184.2 rad / rad/s | undefined | 8.0849e-06 / 0.0069483 | undefined |
| density | 0.01, 0.05, 0.1 | 1.1836 | 6.9205e-06 | 10.84 rad / 1 | undefined | 2.4402e-06 / 0.026456 | undefined |
| area | 0.01, 0.05, 0.1 | 1.2306 | 3.5508e-06 | 0.93866 rad / m² | undefined | 8.4957e-07 / 0.024519 | undefined |
| mass | 0.01, 0.05, 0.1 | 0.0022448 | 1.3138e-06 | 0.0020139 rad / kg | undefined | 0.00021026 / 0.0074617 | undefined |
| inertia | 0.01, 0.05, 0.1 | 1.2363 | 6.6439e-06 | 1.2406 rad / kg·m² (trace, fixed ratios) | undefined | 1.1309e-06 / 0.032848 | undefined |
| cp_y | 0.0001, 0.001, 0.005 | 0.60251 | 9.5392e-06 | 113.89 rad / m | undefined | 2.1461e-06 / 0.0076739 | undefined |
| accommodation | 0.01, 0.03, 0.05 | 0.046853 | 1.0002e-05 | 0.75859 rad / 1 | undefined | 8.9469e-05 / 0.0028809 | undefined |
| specular | 0.01, 0.03, 0.05 | 0.0048508 | 3.3423e-06 | 0.085787 rad / 1 | undefined | 0.00035394 / 7.0793e-06 | undefined |
| diffuse | 0.01, 0.03, 0.05 | 0.011708 | 9.5515e-06 | 0.2274 rad / 1 | undefined | 0.00021019 / 5.9956e-05 | undefined |
| spin | 0.01, 0.05, 0.1 | 1.1516 | 7.7619e-06 | 3718.2 rad / rad/s (fixed direction) | undefined | 2.2454e-06 / 0.090397 | undefined |

### CASE-C, 21600 s — 20/882 resolved cells

**sma_final_m [m]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|
| density | 0.1 | 198.29 | 13.682 | -1879 m / 1 | -0.00027739 | 0.019149 / 0.080701 | 0.11061 |
| area | 0.1 | 198.73 | 15.714 | -158.25 m / m² | -0.00028034 | 0.030921 / 0.075702 | 0.092998 |
| mass | 0.1 | 200.85 | 15.933 | 179.09 m / kg | 0.00026439 | 0.036477 / 0.079759 | 0.24301 |

**mean_projected_area_m2 [m²]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|

No resolved derivative for this observable and horizon.

**attitude_angle_rad [rad]**

| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |
|---|---|---:|---:|---|---:|---|---:|

No resolved derivative for this observable and horizon.

## Linearity, asymmetry and time dependence

Machine-readable evidence retains all three signed magnitudes, every scheme's
slope, neighboring-size disagreement, both one-sided scalar slopes, asymmetry
and time-resolved physical responses. `response_series / delta` gives a
one-sided physical response-norm estimate as a function of time, not a signed
tangent vector or a certified exponential growth rate. Maximum/RMS error
statistics are sampled at common physical times; unsampled extrema are not bounded.
For state vectors, method and size consistency compare vector directions as
well as norms. Scalar one-sided asymmetry is reported; state-only asymmetry
is not independently available from the retained terminal central vectors.
The distinction matters: numerical resolution does not imply linearity over
an exploratory span. A large asymmetric response remains a finite-pair result.

`Cd` scaling is structurally inactive in the selected Sentman force model.
CASE-B starts with zero spin: multiplying that spin creates no perturbation
and no spin-rate-direction derivative; additive body-z rate perturbations are
measured separately. Undefined initial spin direction is never assigned a
manufactured body principal axis.

## Global screening and model contributions

`screen-case-{a,b,c}-v11.json` records seed 20261009, 12 stratified controlled
samples and independent-method/refined-grid checks. Rankings use one common
observable (terminal point-mass SMA), the largest locally resolved tested
span, and three parameters only. A/B use six hours; CASE-C uses its converged
one-hour regime rather than its unresolved six-hour instantaneous dynamics.
This is not a Sobol decomposition or a probability-based importance ranking.
`force-contributions-v11.json` separates complete SRP removal, SRP torque-only
removal (force retained), and gravity-gradient removal in all cases. These
finite model-isolation changes are not infinitesimal parameter derivatives.

## CASE-C mechanism and unresolved six-hour state

Matched continuations show six-hour RK4 0.5→0.25 s final SO(3) disagreement
1.4903 rad for the original case, 2.6054 rad when the common CP offset is
doubled, 0.000290 rad with drag torque alone removed, and 5.90e−11 rad when
the common CP offset is zero. Inertia-ratio continuation also changes the
disagreement. These are controlled evidence of drag/geometry/conditioning
involvement, not proof that a particular six-hour attitude trajectory is physical.
The original baseline's seed gains depend strongly on grid; they are not
certified Lyapunov exponents. Coarse six-hour SMA responses to ±10% density,
area and mass pass the response/derivative gate even though instantaneous
attitude and small sensitivities do not. Do not turn that coarse finite-span
orbital result into centimetre ephemerides or a long-term decay prediction.

## Figures

Six measured figures and their source digest are in `evidence/figures/`: 
timestep_vs_error, convergence_vs_horizon, attitude_sensitivity_vs_time,
perturbation_vs_response, parameter_ranking, uncertainty_vs_response.
Hollow/dashed/unresolved data are diagnostic differences, never resolved
physical sensitivities; zero/undefined channels are explicitly omitted.
