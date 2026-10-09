# Analysis protocol amendment v1.1 — before corrected sweeps

Physical CASE-A/B/C-v1 configurations and the historical benchmark remain
unchanged. This amendment corrects invalid draft analysis, not thresholds chosen
to pass measured results. The withdrawn draft compared derivatives to response
uncertainty, missed relative-quaternion hemisphere selection, and paired seeded
runs with a different parameter configuration. No draft verdict is promoted.

- SO(3) shortest-arc distances lie in [0, pi]; relative rotation vectors are
  expressed in the nominal attitude's local frame. Direction angles use atan2.
- Physical response is max(|J+−J0|,|J−−J0|), in J's units; derivatives divide by
  the actual signed half-width and retain forward/backward asymmetry. Resolution
  compares response, never gradient, with same-unit uncertainty.
- Nominal and perturbed runs use matching numerical settings and parameter
  variants. State sensitivity uses signed Cartesian or local SO(3) central
  vectors; near-pi branch/saturation and null axes are not silently normalized.
- Multi-size signed experiments use production-tight Dopri, controlled RKF78,
  and two identical fixed RK4 grids. Quantitative resolution requires 10:1
  response/envelope and <=20% cross-method AND neighboring-size derivative
  agreement. This is the same predeclared scientific rule, correctly evaluated.
- Fine uncertainty is twice observed pairwise fine differences, with actual
  output-cadence and same-grid mixed-state arithmetic included. Default/coarse
  spread is separate. Sample extrema/ranges are cadence-sensitive diagnostics,
  not terminal-state accuracy; instantaneous SMA and energy are evaluated at
  identical times. Work/impulse quadrature uncertainty remains visible.
- All seven horizons use prefixes of a common six-hour output sequence. This
  preserves identical physical labels and avoids changing clipping history
  merely by stopping early. Full-run counts/costs are labelled as six-hour
  totals. Parameter sensitivity is measured at one and six hours, with
  time-resolved responses at earlier labels; no missing short-horizon slope is
  declared resolved.
- Parameter spans and mission budgets remain exploratory/conditional. No
  unsupported generic CubeSat manufacturing, optics, accommodation or ballistic
  prior is introduced. Characteristic loads come from the actual force model.
- Global screening is gated on locally resolved selected observables and
  refined method agreement, seed20261009, twelve controlled samples. Different
  dimensional derivatives are not compared as a universal ranking.

The numerical kernel and its five prior fixes are unchanged. New diagnostic-tool
regressions distinguish analysis errors from implementation/physical defects.
No caching, new interpolation or performance optimisation is performed.
