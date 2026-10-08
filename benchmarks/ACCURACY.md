# OPT-001 acceptance criteria (fixed before cache implementation)

Direct CSPICE N0067 with DE432s is the reference, Earth observer 399, Sun 10,
J2000, geometric NONE, SI; ET=0. No physics or integrator tolerance changes.

For each 60, 300, 900 and 1800 s interval, require:

| Quantity | Absolute limit |
|---|---:|
| Node position / velocity disagreement | 1e-3 m / 1e-6 m/s |
| Intermediate position maximum (6 h and 30 day coverage) | 1 m |
| Intermediate velocity maximum (6 h and 30 day coverage) | 1e-3 m/s |
| Final orbit position / velocity difference, 6 h | 1e-2 m / 1e-5 m/s |
| Final attitude rotation angle / angular velocity difference, 6 h | 1e-6 rad / 1e-8 rad/s |
| SRP force / body torque difference on reference states | 1e-10 N / 1e-10 N m |
| Illumination difference, including dense penumbra geometry | 1e-6 |
| Cumulative SRP work difference (same 30 s trapezoidal diagnostic) | 1e-5 J |

Also require finite results, exact repeated cache determinism, finite valid nodes,
correct nonmultiple final interval, both endpoints accepted, and unsupported
Sun targets/nonfinite/out-of-coverage epochs rejected. RMS errors are recorded
in addition to maximum errors. Work is a sampled diagnostic, not an additional
integrated state; compare identical output timestamps and quadrature.

All environmental models and current-spacecraft eclipse acquisition remain
unchanged. Adaptive RHS counts may change and must be recorded.

A meaningful speedup requires non-overlapping observed baseline/candidate
runtime ranges on this workload, including preprocessing; otherwise report
inconclusive and keep no performance claim. Choose the coarsest evaluated
interval satisfying all numerical limits; do not relax these limits after
seeing data. For short arcs record preprocessing and estimate break-even
queries rather than implying universal improvement.
