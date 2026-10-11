# ATM-OPT-001 cleanup interleaved provider timing

Configurations are executed round-robin inside each repetition after a full warm-up,
with the per-repetition order recorded. Welch 95% intervals compare each configuration
against the all-off control on per-repetition batch means.

| model | variant | mean ns | median ns | p95 ns | p99 ns | batch SD ns | et2utc/call | str2et/call | pxform/call | spkez/call |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| nrlmsise00 | off_off_off | 31756.3 | 28655.0 | 50925.0 | 66741.8 | 2995.6 | 1.00 | 2.00 | 1.00 | 0.00 |
| nrlmsise00 | utc_only | 10598.2 | 9717.0 | 12142.0 | 24494.0 | 895.7 | 1.00 | 0.00 | 1.00 | 0.00 |
| nrlmsise00 | sun_only | 31175.5 | 29013.0 | 46875.1 | 61359.7 | 1738.2 | 1.00 | 2.00 | 1.00 | 0.00 |
| nrlmsise00 | orientation_only | 28548.0 | 26460.0 | 44414.4 | 52946.2 | 1288.5 | 1.00 | 2.00 | 0.00 | 0.00 |
| nrlmsise00 | utc_orientation | 8355.6 | 7687.0 | 13657.0 | 18864.1 | 1028.7 | 1.00 | 0.00 | 0.00 | 0.00 |
| nrlmsise00 | utc_sun | 10716.8 | 9772.0 | 17551.4 | 24834.6 | 1290.0 | 1.00 | 0.00 | 1.00 | 0.00 |
| nrlmsise00 | sun_orientation | 29698.0 | 26460.0 | 41806.1 | 56776.2 | 5173.4 | 1.00 | 2.00 | 0.00 | 0.00 |
| nrlmsise00 | all | 8198.4 | 7669.0 | 11746.0 | 18248.1 | 503.7 | 1.00 | 0.00 | 0.00 | 0.00 |
| jb2008 | off_off_off | 36602.5 | 34293.0 | 52671.5 | 69414.2 | 2402.2 | 1.00 | 2.00 | 1.00 | 1.00 |
| jb2008 | utc_only | 16273.3 | 15466.0 | 21218.2 | 32934.6 | 438.0 | 1.00 | 0.00 | 1.00 | 1.00 |
| jb2008 | sun_only | 36181.5 | 34293.0 | 48649.9 | 63067.1 | 725.0 | 1.00 | 2.00 | 1.00 | 1.00 |
| jb2008 | orientation_only | 34027.7 | 32227.0 | 46894.5 | 62703.0 | 567.1 | 1.00 | 2.00 | 0.00 | 1.00 |
| jb2008 | utc_orientation | 14025.2 | 13464.0 | 16871.0 | 27691.1 | 282.3 | 1.00 | 0.00 | 0.00 | 1.00 |
| jb2008 | utc_sun | 16281.5 | 15457.0 | 19700.0 | 33347.0 | 345.9 | 1.00 | 0.00 | 1.00 | 1.00 |
| jb2008 | sun_orientation | 33872.9 | 32218.0 | 45562.9 | 60495.0 | 544.7 | 1.00 | 2.00 | 0.00 | 1.00 |
| jb2008 | all | 14052.1 | 13473.0 | 16798.0 | 28205.3 | 222.0 | 1.00 | 0.00 | 0.00 | 1.00 |

## Sun-toggle test

```json
{
  "utc_only_vs_utc_sun": {
    "identical_operation_counts": true,
    "spkez_calls_per_call": [
      0.0,
      0.0
    ],
    "mean_difference_ns": -118.81774999999834,
    "t_statistic": -0.2620919700018066,
    "ci95_ns": [
      -1007.3714512003498,
      769.7359512003532
    ],
    "difference_distinguishable_from_variability": false
  },
  "orientation_only_vs_sun_orientation": {
    "identical_operation_counts": true,
    "spkez_calls_per_call": [
      0.0,
      0.0
    ],
    "mean_difference_ns": -1150.1498750000028,
    "t_statistic": -0.7473090885720408,
    "ci95_ns": [
      -4166.697899737013,
      1866.3981497370078
    ],
    "difference_distinguishable_from_variability": false
  }
}
```

## Verification

```json
{
  "all_configurations_bitwise_identical": true,
  "all_equal_to_unchanged_original": true,
  "models": [
    "nrlmsise00",
    "jb2008"
  ],
  "nrlmsise00_sun_toggle_identical_operation_counts": true,
  "nrlmsise00_spkez_calls_per_call": 0.0,
  "nrlmsise00_sun_toggle_verdict": "NRLMSISE-00 performs no Sun lookup in any configuration, so sun_sharing changes no operation and no result; every timing difference is scheduling/variability only"
}
```
