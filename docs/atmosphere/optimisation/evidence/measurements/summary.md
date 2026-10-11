# ATM-OPT-001 measured optimisation summary

Every number below comes from `summary.json`; the raw per-call samples remain in
the benchmark documents and the manifest records the exact commands, environment,
dependency hashes and preserved evidence hashes.

## Fixed full provider (per call, mean of all samples)

| model | variant | original ns | candidate ns | speedup | p95 ns | p99 ns | batch spread |
|---|---|---:|---:|---:|---:|---:|---:|
| exponential | off_off_off | 58.7 | 69.6 | 0.844x | 74.0 | 92.0 | 0.025 |
| exponential | utc_only | 58.7 | 86.1 | 0.682x | 92.0 | 92.0 | 0.108 |
| exponential | sun_only | 58.7 | 72.0 | 0.816x | 83.0 | 92.0 | 0.033 |
| exponential | orientation_only | 58.7 | 68.4 | 0.858x | 74.0 | 74.0 | 0.003 |
| exponential | utc_sun | 58.7 | 70.2 | 0.837x | 74.0 | 74.0 | 0.064 |
| exponential | utc_orientation | 58.7 | 69.8 | 0.841x | 74.0 | 74.0 | 0.059 |
| exponential | sun_orientation | 58.7 | 72.5 | 0.810x | 74.0 | 82.0 | 0.159 |
| exponential | all | 58.7 | 68.8 | 0.853x | 74.0 | 74.0 | 0.063 |
| jb2008 | off_off_off | 39131.3 | 44620.9 | 0.877x | 71829.0 | 94220.1 | 0.086 |
| jb2008 | utc_only | 39131.3 | 19249.0 | 2.033x | 29811.5 | 44470.1 | 0.060 |
| jb2008 | sun_only | 39131.3 | 37294.0 | 1.049x | 54627.9 | 73877.9 | 0.060 |
| jb2008 | orientation_only | 39131.3 | 37149.5 | 1.053x | 50909.7 | 75119.2 | 0.045 |
| jb2008 | utc_sun | 39131.3 | 16402.2 | 2.386x | 25339.0 | 40431.0 | 0.122 |
| jb2008 | utc_orientation | 39131.3 | 13955.2 | 2.804x | 14530.8 | 27645.6 | 0.016 |
| jb2008 | sun_orientation | 39131.3 | 33292.8 | 1.175x | 48749.9 | 58925.1 | 0.046 |
| jb2008 | all | 39131.3 | 12142.5 | 3.223x | 18331.0 | 25202.0 | 0.092 |
| nrlmsise00 | off_off_off | 30449.6 | 34167.7 | 0.891x | 50827.3 | 69055.2 | 0.066 |
| nrlmsise00 | utc_only | 30449.6 | 11652.2 | 2.613x | 16623.0 | 31134.1 | 0.017 |
| nrlmsise00 | sun_only | 30449.6 | 37536.2 | 0.811x | 60321.0 | 89040.0 | 0.060 |
| nrlmsise00 | orientation_only | 30449.6 | 28890.5 | 1.054x | 35378.8 | 53847.1 | 0.022 |
| nrlmsise00 | utc_sun | 30449.6 | 10133.0 | 3.005x | 10249.0 | 22253.9 | 0.024 |
| nrlmsise00 | utc_orientation | 30449.6 | 8176.7 | 3.724x | 12169.0 | 18728.7 | 0.063 |
| nrlmsise00 | sun_orientation | 30449.6 | 30227.0 | 1.007x | 45049.3 | 62360.2 | 0.071 |
| nrlmsise00 | all | 30449.6 | 9543.3 | 3.191x | 14502.4 | 27979.5 | 0.042 |

## Isolated lookup cost (mean ns per call)

| operation | mean ns | median ns | p99 ns |
|---|---:|---:|---:|
| utc_fixed_reuse | 4537.2 | 4361.0 | 8372.5 |
| utc_fixed_noreuse | 25702.5 | 25117.0 | 45414.1 |
| utc_changing | 26586.7 | 25656.0 | 49137.2 |
| orientation_fixed_reuse | 32.5 | 30.0 | 50.0 |
| orientation_fixed_noreuse | 2072.4 | 1996.0 | 2505.0 |
| orientation_changing | 2060.9 | 2036.0 | 2116.0 |
| sun_observation | 2053.4 | 2015.0 | 2235.2 |
| sun_geometric | 2041.6 | 1976.0 | 2535.0 |
| provider_control | 39490.4 | 37591.0 | 72348.9 |
| provider_full | 14786.4 | 14250.0 | 29129.3 |

### utc lookup

```json
{
  "reuse_mean_ns": 4537.1534,
  "no_reuse_mean_ns": 25702.4966,
  "saved_work_ns": 21165.3432,
  "hit_rate": null,
  "miss_path_mean_ns": 26586.7135
}
```

### orientation lookup

```json
{
  "reuse_mean_ns": 32.4935,
  "no_reuse_mean_ns": 2072.4385,
  "saved_work_ns": 2039.9450000000002,
  "hit_rate": null,
  "miss_path_mean_ns": 2060.9286
}
```

### sun lookup

```json
{
  "observation_mean_ns": 2053.4406,
  "geometric_state_mean_ns": 2041.5838,
  "shared_difference_ns": 11.856799999999794
}
```

## Six-hour CASE propagation (median of the repeats)

| case | model | variant | original s | candidate s | speedup | spread | UTC hits | str2et | spkez |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| CASE-A | exponential | off_off_off | 0.173 | 0.166 | 1.044x | 0.034 | 0 | 0 | 182550 |
| CASE-A | exponential | utc_only | 0.173 | 0.191 | 0.908x | 0.056 | 0 | 0 | 182550 |
| CASE-A | exponential | sun_only | 0.173 | 0.181 | 0.958x | 0.030 | 0 | 0 | 182550 |
| CASE-A | exponential | orientation_only | 0.173 | 0.265 | 0.653x | 0.151 | 0 | 0 | 182550 |
| CASE-A | exponential | utc_sun | 0.173 | 0.197 | 0.879x | 0.141 | 0 | 0 | 182550 |
| CASE-A | exponential | utc_orientation | 0.173 | 0.170 | 1.018x | 0.052 | 0 | 0 | 182550 |
| CASE-A | exponential | sun_orientation | 0.173 | 0.172 | 1.008x | 0.033 | 0 | 0 | 182550 |
| CASE-A | exponential | all | 0.173 | 0.166 | 1.043x | 0.025 | 0 | 0 | 182550 |
| CASE-A | jb2008 | off_off_off | 2.504 | 2.758 | 0.908x | 0.072 | 0 | 365454 | 365454 |
| CASE-A | jb2008 | utc_only | 2.504 | 1.422 | 1.762x | 0.052 | 182727 | 0 | 365454 |
| CASE-A | jb2008 | sun_only | 2.504 | 3.521 | 0.711x | 0.068 | 0 | 365454 | 182727 |
| CASE-A | jb2008 | orientation_only | 2.504 | 2.772 | 0.904x | 0.102 | 0 | 365454 | 365454 |
| CASE-A | jb2008 | utc_sun | 2.504 | 1.262 | 1.985x | 0.095 | 182727 | 0 | 182727 |
| CASE-A | jb2008 | utc_orientation | 2.504 | 1.182 | 2.119x | 0.025 | 182727 | 0 | 365454 |
| CASE-A | jb2008 | sun_orientation | 2.504 | 2.486 | 1.008x | 0.070 | 0 | 365454 | 182727 |
| CASE-A | jb2008 | all | 2.504 | 1.032 | 2.427x | 0.009 | 182727 | 0 | 182727 |
| CASE-A | nrlmsise00 | off_off_off | 2.092 | 2.164 | 0.967x | 0.004 | 0 | 365088 | 182544 |
| CASE-A | nrlmsise00 | utc_only | 2.092 | 0.933 | 2.242x | 0.013 | 182544 | 0 | 182544 |
| CASE-A | nrlmsise00 | sun_only | 2.092 | 2.395 | 0.873x | 0.129 | 0 | 365088 | 182544 |
| CASE-A | nrlmsise00 | orientation_only | 2.092 | 4.678 | 0.447x | 0.169 | 0 | 365088 | 182544 |
| CASE-A | nrlmsise00 | utc_sun | 2.092 | 0.846 | 2.473x | 0.002 | 182544 | 0 | 182544 |
| CASE-A | nrlmsise00 | utc_orientation | 2.092 | 0.763 | 2.742x | 0.082 | 182544 | 0 | 182544 |
| CASE-A | nrlmsise00 | sun_orientation | 2.092 | 2.366 | 0.884x | 0.024 | 0 | 365088 | 182544 |
| CASE-A | nrlmsise00 | all | 2.092 | 0.765 | 2.734x | 0.012 | 182544 | 0 | 182544 |
| CASE-B | exponential | off_off_off | 0.031 | 0.030 | 1.013x | 0.012 | 0 | 0 | 33963 |
| CASE-B | exponential | utc_only | 0.031 | 0.030 | 1.019x | 0.016 | 0 | 0 | 33963 |
| CASE-B | exponential | sun_only | 0.031 | 0.030 | 1.030x | 0.007 | 0 | 0 | 33963 |
| CASE-B | exponential | orientation_only | 0.031 | 0.035 | 0.877x | 0.052 | 0 | 0 | 33963 |
| CASE-B | exponential | utc_sun | 0.031 | 0.033 | 0.942x | 0.014 | 0 | 0 | 33963 |
| CASE-B | exponential | utc_orientation | 0.031 | 0.030 | 1.027x | 0.012 | 0 | 0 | 33963 |
| CASE-B | exponential | sun_orientation | 0.031 | 0.032 | 0.954x | 0.035 | 0 | 0 | 33963 |
| CASE-B | exponential | all | 0.031 | 0.034 | 0.893x | 0.035 | 0 | 0 | 33963 |
| CASE-B | jb2008 | off_off_off | 1.733 | 1.611 | 1.076x | 0.003 | 0 | 235506 | 235506 |
| CASE-B | jb2008 | utc_only | 1.733 | 0.768 | 2.257x | 0.006 | 117753 | 0 | 235506 |
| CASE-B | jb2008 | sun_only | 1.733 | 1.703 | 1.018x | 0.050 | 0 | 235506 | 117753 |
| CASE-B | jb2008 | orientation_only | 1.733 | 1.709 | 1.014x | 0.072 | 0 | 235506 | 235506 |
| CASE-B | jb2008 | utc_sun | 1.733 | 0.697 | 2.488x | 0.006 | 117753 | 0 | 117753 |
| CASE-B | jb2008 | utc_orientation | 1.733 | 0.801 | 2.164x | 0.026 | 117753 | 0 | 235506 |
| CASE-B | jb2008 | sun_orientation | 1.733 | 1.617 | 1.071x | 0.023 | 0 | 235506 | 117753 |
| CASE-B | jb2008 | all | 1.733 | 0.766 | 2.262x | 0.035 | 117753 | 0 | 117753 |
| CASE-B | nrlmsise00 | off_off_off | 0.434 | 0.432 | 1.006x | 0.030 | 0 | 74454 | 37227 |
| CASE-B | nrlmsise00 | utc_only | 0.434 | 0.161 | 2.701x | 0.008 | 37227 | 0 | 37227 |
| CASE-B | nrlmsise00 | sun_only | 0.434 | 0.433 | 1.002x | 0.013 | 0 | 74454 | 37227 |
| CASE-B | nrlmsise00 | orientation_only | 0.434 | 0.521 | 0.834x | 0.006 | 0 | 74454 | 37227 |
| CASE-B | nrlmsise00 | utc_sun | 0.434 | 0.180 | 2.415x | 0.031 | 37227 | 0 | 37227 |
| CASE-B | nrlmsise00 | utc_orientation | 0.434 | 0.153 | 2.844x | 0.015 | 37227 | 0 | 37227 |
| CASE-B | nrlmsise00 | sun_orientation | 0.434 | 0.467 | 0.929x | 0.043 | 0 | 74454 | 37227 |
| CASE-B | nrlmsise00 | all | 0.434 | 0.166 | 2.613x | 0.046 | 37227 | 0 | 37227 |
| CASE-C | exponential | off_off_off | 0.138 | 0.147 | 0.939x | 0.098 | 0 | 0 | 139047 |
| CASE-C | exponential | utc_only | 0.138 | 0.138 | 1.003x | 0.052 | 0 | 0 | 139047 |
| CASE-C | exponential | sun_only | 0.138 | 0.139 | 0.995x | 0.061 | 0 | 0 | 139047 |
| CASE-C | exponential | orientation_only | 0.138 | 0.128 | 1.082x | 0.015 | 0 | 0 | 139047 |
| CASE-C | exponential | utc_sun | 0.138 | 0.149 | 0.931x | 0.034 | 0 | 0 | 139047 |
| CASE-C | exponential | utc_orientation | 0.138 | 0.169 | 0.817x | 0.087 | 0 | 0 | 139047 |
| CASE-C | exponential | sun_orientation | 0.138 | 0.161 | 0.860x | 0.057 | 0 | 0 | 139047 |
| CASE-C | exponential | all | 0.138 | 0.135 | 1.026x | 0.022 | 0 | 0 | 139047 |
| CASE-C | jb2008 | off_off_off | 5.091 | 5.502 | 0.925x | 0.033 | 0 | 728688 | 728688 |
| CASE-C | jb2008 | utc_only | 5.091 | 2.348 | 2.168x | 0.028 | 364344 | 0 | 728688 |
| CASE-C | jb2008 | sun_only | 5.091 | 4.750 | 1.072x | 0.019 | 0 | 728688 | 364344 |
| CASE-C | jb2008 | orientation_only | 5.091 | 5.559 | 0.916x | 0.067 | 0 | 728688 | 728688 |
| CASE-C | jb2008 | utc_sun | 5.091 | 2.252 | 2.261x | 0.014 | 364344 | 0 | 364344 |
| CASE-C | jb2008 | utc_orientation | 5.091 | 2.521 | 2.019x | 0.048 | 364344 | 0 | 728688 |
| CASE-C | jb2008 | sun_orientation | 5.091 | 4.851 | 1.049x | 0.039 | 0 | 728688 | 364344 |
| CASE-C | jb2008 | all | 5.091 | 2.168 | 2.349x | 0.008 | 364344 | 0 | 364344 |
| CASE-C | nrlmsise00 | off_off_off | 2.949 | 3.202 | 0.921x | 0.070 | 0 | 504828 | 252414 |
| CASE-C | nrlmsise00 | utc_only | 2.949 | 1.170 | 2.520x | 0.086 | 252414 | 0 | 252414 |
| CASE-C | nrlmsise00 | sun_only | 2.949 | 2.995 | 0.985x | 0.069 | 0 | 504828 | 252414 |
| CASE-C | nrlmsise00 | orientation_only | 2.949 | 3.074 | 0.959x | 0.026 | 0 | 504828 | 252414 |
| CASE-C | nrlmsise00 | utc_sun | 2.949 | 1.250 | 2.360x | 0.059 | 252414 | 0 | 252414 |
| CASE-C | nrlmsise00 | utc_orientation | 2.949 | 1.202 | 2.453x | 0.022 | 252414 | 0 | 252414 |
| CASE-C | nrlmsise00 | sun_orientation | 2.949 | 2.995 | 0.985x | 0.058 | 0 | 504828 | 252414 |
| CASE-C | nrlmsise00 | all | 2.949 | 1.102 | 2.675x | 0.003 | 252414 | 0 | 252414 |

## Concurrency (throughput calls/s, mean over batches)

| model | variant | threads | original | candidate | ratio | median latency ns | contended | wait ns/acquisition |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| exponential | off_off_off | 1 | 7630444 | 7493367 | 0.982x | 70.0 | n/a | n/a |
| exponential | off_off_off | 2 | 13715008 | 13123791 | 0.957x | 60.0 | n/a | n/a |
| exponential | off_off_off | 4 | 15907972 | 15675931 | 0.985x | 60.0 | n/a | n/a |
| exponential | utc_only | 1 | 7630444 | 7901786 | 1.036x | 60.0 | n/a | n/a |
| exponential | utc_only | 2 | 13715008 | 12675364 | 0.924x | 60.0 | n/a | n/a |
| exponential | utc_only | 4 | 15907972 | 15297037 | 0.962x | 60.0 | n/a | n/a |
| exponential | sun_only | 1 | 7630444 | 7527745 | 0.987x | 70.0 | n/a | n/a |
| exponential | sun_only | 2 | 13715008 | 12847583 | 0.937x | 60.0 | n/a | n/a |
| exponential | sun_only | 4 | 15907972 | 16844488 | 1.059x | 60.0 | n/a | n/a |
| exponential | orientation_only | 1 | 7630444 | 8249841 | 1.081x | 60.0 | n/a | n/a |
| exponential | orientation_only | 2 | 13715008 | 12141590 | 0.885x | 60.0 | n/a | n/a |
| exponential | orientation_only | 4 | 15907972 | 15536599 | 0.977x | 60.0 | n/a | n/a |
| exponential | utc_sun | 1 | 7630444 | 8023272 | 1.051x | 60.0 | n/a | n/a |
| exponential | utc_sun | 2 | 13715008 | 12982674 | 0.947x | 60.0 | n/a | n/a |
| exponential | utc_sun | 4 | 15907972 | 15780567 | 0.992x | 60.0 | n/a | n/a |
| exponential | utc_orientation | 1 | 7630444 | 8752680 | 1.147x | 60.0 | n/a | n/a |
| exponential | utc_orientation | 2 | 13715008 | 12921874 | 0.942x | 60.0 | n/a | n/a |
| exponential | utc_orientation | 4 | 15907972 | 16533403 | 1.039x | 60.0 | n/a | n/a |
| exponential | sun_orientation | 1 | 7630444 | 8208743 | 1.076x | 60.0 | n/a | n/a |
| exponential | sun_orientation | 2 | 13715008 | 13110972 | 0.956x | 60.0 | n/a | n/a |
| exponential | sun_orientation | 4 | 15907972 | 15211285 | 0.956x | 60.0 | n/a | n/a |
| exponential | all | 1 | 7630444 | 7628308 | 1.000x | 60.0 | n/a | n/a |
| exponential | all | 2 | 13715008 | 12483482 | 0.910x | 60.0 | n/a | n/a |
| exponential | all | 4 | 15907972 | 16237115 | 1.021x | 60.0 | n/a | n/a |
| jb2008 | off_off_off | 1 | 26823 | 26697 | 0.995x | 34900.0 | 0.000 | 29 |
| jb2008 | off_off_off | 2 | 12250 | 12004 | 0.980x | 165282.0 | 0.194 | 16416 |
| jb2008 | off_off_off | 4 | 10782 | 11413 | 1.059x | 274636.5 | 0.253 | 55814 |
| jb2008 | utc_only | 1 | 26823 | 57270 | 2.135x | 16935.0 | 0.000 | 29 |
| jb2008 | utc_only | 2 | 12250 | 20933 | 1.709x | 85577.5 | 0.224 | 9986 |
| jb2008 | utc_only | 4 | 10782 | 22187 | 2.058x | 121770.0 | 0.254 | 24326 |
| jb2008 | sun_only | 1 | 26823 | 27728 | 1.034x | 33944.0 | 0.000 | 29 |
| jb2008 | sun_only | 2 | 12250 | 11614 | 0.948x | 86625.0 | 0.055 | 15126 |
| jb2008 | sun_only | 4 | 10782 | 10559 | 0.979x | 92511.0 | 0.092 | 51715 |
| jb2008 | orientation_only | 1 | 26823 | 24281 | 0.905x | 36902.0 | 0.000 | 29 |
| jb2008 | orientation_only | 2 | 12250 | 10682 | 0.872x | 178135.0 | 0.192 | 15570 |
| jb2008 | orientation_only | 4 | 10782 | 11918 | 1.105x | 130621.0 | 0.214 | 42798 |
| jb2008 | utc_sun | 1 | 26823 | 67114 | 2.502x | 13822.0 | 0.000 | 28 |
| jb2008 | utc_sun | 2 | 12250 | 26955 | 2.200x | 71003.0 | 0.125 | 4505 |
| jb2008 | utc_sun | 4 | 10782 | 31045 | 2.879x | 100933.0 | 0.262 | 14597 |
| jb2008 | utc_orientation | 1 | 26823 | 60362 | 2.250x | 15276.0 | 0.000 | 29 |
| jb2008 | utc_orientation | 2 | 12250 | 25890 | 2.113x | 79361.0 | 0.212 | 7597 |
| jb2008 | utc_orientation | 4 | 10782 | 26639 | 2.471x | 109883.0 | 0.256 | 15036 |
| jb2008 | sun_orientation | 1 | 26823 | 26011 | 0.970x | 36504.0 | 0.000 | 30 |
| jb2008 | sun_orientation | 2 | 12250 | 12776 | 1.043x | 85055.0 | 0.053 | 14343 |
| jb2008 | sun_orientation | 4 | 10782 | 10893 | 1.010x | 94062.0 | 0.080 | 44989 |
| jb2008 | all | 1 | 26823 | 81611 | 3.043x | 11754.0 | 0.000 | 30 |
| jb2008 | all | 2 | 12250 | 27933 | 2.280x | 40421.5 | 0.053 | 1891 |
| jb2008 | all | 4 | 10782 | 33052 | 3.065x | 49369.0 | 0.143 | 6857 |
| nrlmsise00 | off_off_off | 1 | 31529 | 29162 | 0.925x | 32032.0 | 0.000 | 31 |
| nrlmsise00 | off_off_off | 2 | 13477 | 11750 | 0.872x | 86630.0 | 0.077 | 18548 |
| nrlmsise00 | off_off_off | 4 | 12928 | 11129 | 0.861x | 95052.0 | 0.189 | 54358 |
| nrlmsise00 | utc_only | 1 | 31529 | 87521 | 2.776x | 10586.0 | 0.000 | 29 |
| nrlmsise00 | utc_only | 2 | 13477 | 24234 | 1.798x | 69012.0 | 0.185 | 10244 |
| nrlmsise00 | utc_only | 4 | 12928 | 26671 | 2.063x | 107445.0 | 0.277 | 24791 |
| nrlmsise00 | sun_only | 1 | 31529 | 27711 | 0.879x | 33081.0 | 0.000 | 30 |
| nrlmsise00 | sun_only | 2 | 13477 | 12051 | 0.894x | 85155.0 | 0.069 | 17829 |
| nrlmsise00 | sun_only | 4 | 12928 | 12403 | 0.959x | 92116.5 | 0.193 | 54939 |
| nrlmsise00 | orientation_only | 1 | 31529 | 35004 | 1.110x | 27240.0 | 0.000 | 33 |
| nrlmsise00 | orientation_only | 2 | 13477 | 13426 | 0.996x | 75181.0 | 0.037 | 19321 |
| nrlmsise00 | orientation_only | 4 | 12928 | 12330 | 0.954x | 81349.0 | 0.055 | 54014 |
| nrlmsise00 | utc_sun | 1 | 31529 | 89232 | 2.830x | 10583.0 | 0.000 | 29 |
| nrlmsise00 | utc_sun | 2 | 13477 | 26717 | 1.982x | 62491.0 | 0.199 | 10735 |
| nrlmsise00 | utc_sun | 4 | 12928 | 29659 | 2.294x | 110739.0 | 0.276 | 26561 |
| nrlmsise00 | utc_orientation | 1 | 31529 | 115823 | 3.674x | 7974.0 | 0.000 | 29 |
| nrlmsise00 | utc_orientation | 2 | 13477 | 32623 | 2.421x | 32428.0 | 0.050 | 8385 |
| nrlmsise00 | utc_orientation | 4 | 12928 | 40285 | 3.116x | 35532.0 | 0.113 | 16884 |
| nrlmsise00 | sun_orientation | 1 | 31529 | 31272 | 0.992x | 30087.0 | 0.000 | 29 |
| nrlmsise00 | sun_orientation | 2 | 13477 | 13685 | 1.015x | 72913.0 | 0.038 | 18518 |
| nrlmsise00 | sun_orientation | 4 | 12928 | 13491 | 1.044x | 74125.0 | 0.051 | 53203 |
| nrlmsise00 | all | 1 | 31529 | 122566 | 3.887x | 7412.0 | 0.000 | 30 |
| nrlmsise00 | all | 2 | 13477 | 35164 | 2.609x | 31061.0 | 0.055 | 8769 |
| nrlmsise00 | all | 4 | 12928 | 44124 | 3.413x | 32778.0 | 0.115 | 17494 |

## Deterministic locality schedules (mean ns per call, speedup vs original)

| schedule | model | variant | original ns | candidate ns | speedup | UTC hit rate | orientation hit rate |
|---|---|---|---:|---:|---:|---:|---:|
| ensemble_epoch_major | exponential | off_off_off | 2094.0 | 2009.7 | 1.042x | n/a | n/a |
| ensemble_epoch_major | exponential | utc_only | 2094.0 | 1977.6 | 1.059x | n/a | n/a |
| ensemble_epoch_major | exponential | sun_only | 2094.0 | 1967.6 | 1.064x | n/a | n/a |
| ensemble_epoch_major | exponential | orientation_only | 2094.0 | 1979.4 | 1.058x | n/a | n/a |
| ensemble_epoch_major | exponential | utc_sun | 2094.0 | 1982.2 | 1.056x | n/a | n/a |
| ensemble_epoch_major | exponential | utc_orientation | 2094.0 | 1959.9 | 1.068x | n/a | n/a |
| ensemble_epoch_major | exponential | sun_orientation | 2094.0 | 2032.9 | 1.030x | n/a | n/a |
| ensemble_epoch_major | exponential | all | 2094.0 | 1966.1 | 1.065x | n/a | n/a |
| ensemble_epoch_major | jb2008 | off_off_off | 42051.5 | 38315.0 | 1.098x | 0.0000 | 0.0000 |
| ensemble_epoch_major | jb2008 | utc_only | 42051.5 | 17810.9 | 2.361x | 1.0000 | 0.0000 |
| ensemble_epoch_major | jb2008 | sun_only | 42051.5 | 36677.4 | 1.147x | 0.0000 | 0.0000 |
| ensemble_epoch_major | jb2008 | orientation_only | 42051.5 | 38582.1 | 1.090x | 0.0000 | 0.8760 |
| ensemble_epoch_major | jb2008 | utc_sun | 42051.5 | 16653.0 | 2.525x | 1.0000 | 0.0000 |
| ensemble_epoch_major | jb2008 | utc_orientation | 42051.5 | 16358.5 | 2.571x | 1.0000 | 0.8760 |
| ensemble_epoch_major | jb2008 | sun_orientation | 42051.5 | 37406.4 | 1.124x | 0.0000 | 0.8760 |
| ensemble_epoch_major | jb2008 | all | 42051.5 | 14499.6 | 2.900x | 1.0000 | 0.8760 |
| ensemble_epoch_major | nrlmsise00 | off_off_off | 32286.7 | 35557.8 | 0.908x | 0.0000 | 0.0000 |
| ensemble_epoch_major | nrlmsise00 | utc_only | 32286.7 | 12712.3 | 2.540x | 1.0000 | 0.0000 |
| ensemble_epoch_major | nrlmsise00 | sun_only | 32286.7 | 35307.3 | 0.914x | 0.0000 | 0.0000 |
| ensemble_epoch_major | nrlmsise00 | orientation_only | 32286.7 | 31754.3 | 1.017x | 0.0000 | 0.8760 |
| ensemble_epoch_major | nrlmsise00 | utc_sun | 32286.7 | 13634.6 | 2.368x | 1.0000 | 0.0000 |
| ensemble_epoch_major | nrlmsise00 | utc_orientation | 32286.7 | 11073.5 | 2.916x | 1.0000 | 0.8760 |
| ensemble_epoch_major | nrlmsise00 | sun_orientation | 32286.7 | 33129.2 | 0.975x | 0.0000 | 0.8760 |
| ensemble_epoch_major | nrlmsise00 | all | 32286.7 | 10304.8 | 3.133x | 1.0000 | 0.8760 |
| ensemble_trajectory_major | exponential | off_off_off | 1971.4 | 2055.5 | 0.959x | n/a | n/a |
| ensemble_trajectory_major | exponential | utc_only | 1971.4 | 2056.9 | 0.958x | n/a | n/a |
| ensemble_trajectory_major | exponential | sun_only | 1971.4 | 2069.2 | 0.953x | n/a | n/a |
| ensemble_trajectory_major | exponential | orientation_only | 1971.4 | 2041.1 | 0.966x | n/a | n/a |
| ensemble_trajectory_major | exponential | utc_sun | 1971.4 | 1913.0 | 1.031x | n/a | n/a |
| ensemble_trajectory_major | exponential | utc_orientation | 1971.4 | 1855.0 | 1.063x | n/a | n/a |
| ensemble_trajectory_major | exponential | sun_orientation | 1971.4 | 1928.3 | 1.022x | n/a | n/a |
| ensemble_trajectory_major | exponential | all | 1971.4 | 1911.2 | 1.032x | n/a | n/a |
| ensemble_trajectory_major | jb2008 | off_off_off | 38144.7 | 40663.0 | 0.938x | 0.0000 | 0.0000 |
| ensemble_trajectory_major | jb2008 | utc_only | 38144.7 | 19137.0 | 1.993x | 1.0000 | 0.0000 |
| ensemble_trajectory_major | jb2008 | sun_only | 38144.7 | 38458.1 | 0.992x | 0.0000 | 0.0000 |
| ensemble_trajectory_major | jb2008 | orientation_only | 38144.7 | 38865.4 | 0.981x | 0.0000 | 0.7393 |
| ensemble_trajectory_major | jb2008 | utc_sun | 38144.7 | 16271.3 | 2.344x | 1.0000 | 0.0000 |
| ensemble_trajectory_major | jb2008 | utc_orientation | 38144.7 | 17885.4 | 2.133x | 1.0000 | 0.7393 |
| ensemble_trajectory_major | jb2008 | sun_orientation | 38144.7 | 41052.3 | 0.929x | 0.0000 | 0.7393 |
| ensemble_trajectory_major | jb2008 | all | 38144.7 | 15037.2 | 2.537x | 1.0000 | 0.7393 |
| ensemble_trajectory_major | nrlmsise00 | off_off_off | 32520.2 | 32927.1 | 0.988x | 0.0000 | 0.0000 |
| ensemble_trajectory_major | nrlmsise00 | utc_only | 32520.2 | 12371.3 | 2.629x | 1.0000 | 0.0000 |
| ensemble_trajectory_major | nrlmsise00 | sun_only | 32520.2 | 33142.4 | 0.981x | 0.0000 | 0.0000 |
| ensemble_trajectory_major | nrlmsise00 | orientation_only | 32520.2 | 33057.0 | 0.984x | 0.0000 | 0.7393 |
| ensemble_trajectory_major | nrlmsise00 | utc_sun | 32520.2 | 13018.2 | 2.498x | 1.0000 | 0.0000 |
| ensemble_trajectory_major | nrlmsise00 | utc_orientation | 32520.2 | 10951.2 | 2.970x | 1.0000 | 0.7393 |
| ensemble_trajectory_major | nrlmsise00 | sun_orientation | 32520.2 | 32361.5 | 1.005x | 0.0000 | 0.7393 |
| ensemble_trajectory_major | nrlmsise00 | all | 32520.2 | 11164.7 | 2.913x | 1.0000 | 0.7393 |
| fixed | exponential | off_off_off | 2045.8 | 2066.2 | 0.990x | n/a | n/a |
| fixed | exponential | utc_only | 2045.8 | 2066.6 | 0.990x | n/a | n/a |
| fixed | exponential | sun_only | 2045.8 | 2443.2 | 0.837x | n/a | n/a |
| fixed | exponential | orientation_only | 2045.8 | 2104.6 | 0.972x | n/a | n/a |
| fixed | exponential | utc_sun | 2045.8 | 2366.2 | 0.865x | n/a | n/a |
| fixed | exponential | utc_orientation | 2045.8 | 2443.6 | 0.837x | n/a | n/a |
| fixed | exponential | sun_orientation | 2045.8 | 2500.0 | 0.818x | n/a | n/a |
| fixed | exponential | all | 2045.8 | 2367.4 | 0.864x | n/a | n/a |
| fixed | jb2008 | off_off_off | 37572.1 | 42283.7 | 0.889x | 0.0000 | 0.0000 |
| fixed | jb2008 | utc_only | 37572.1 | 20053.0 | 1.874x | 1.0000 | 0.0000 |
| fixed | jb2008 | sun_only | 37572.1 | 38414.4 | 0.978x | 0.0000 | 0.0000 |
| fixed | jb2008 | orientation_only | 37572.1 | 36817.5 | 1.020x | 0.0000 | 1.0000 |
| fixed | jb2008 | utc_sun | 37572.1 | 15818.2 | 2.375x | 1.0000 | 0.0000 |
| fixed | jb2008 | utc_orientation | 37572.1 | 15899.5 | 2.363x | 1.0000 | 1.0000 |
| fixed | jb2008 | sun_orientation | 37572.1 | 36064.4 | 1.042x | 0.0000 | 1.0000 |
| fixed | jb2008 | all | 37572.1 | 14290.4 | 2.629x | 1.0000 | 1.0000 |
| fixed | nrlmsise00 | off_off_off | 34603.8 | 34587.6 | 1.000x | 0.0000 | 0.0000 |
| fixed | nrlmsise00 | utc_only | 34603.8 | 12761.4 | 2.712x | 1.0000 | 0.0000 |
| fixed | nrlmsise00 | sun_only | 34603.8 | 33804.9 | 1.024x | 0.0000 | 0.0000 |
| fixed | nrlmsise00 | orientation_only | 34603.8 | 31075.9 | 1.114x | 0.0000 | 1.0000 |
| fixed | nrlmsise00 | utc_sun | 34603.8 | 13075.1 | 2.647x | 1.0000 | 0.0000 |
| fixed | nrlmsise00 | utc_orientation | 34603.8 | 14558.6 | 2.377x | 1.0000 | 1.0000 |
| fixed | nrlmsise00 | sun_orientation | 34603.8 | 32179.0 | 1.075x | 0.0000 | 1.0000 |
| fixed | nrlmsise00 | all | 34603.8 | 10488.7 | 3.299x | 1.0000 | 1.0000 |
| identical_daily_records_changing_geometry | exponential | off_off_off | 2087.3 | 2019.9 | 1.033x | n/a | n/a |
| identical_daily_records_changing_geometry | exponential | utc_only | 2087.3 | 2169.0 | 0.962x | n/a | n/a |
| identical_daily_records_changing_geometry | exponential | sun_only | 2087.3 | 2351.7 | 0.888x | n/a | n/a |
| identical_daily_records_changing_geometry | exponential | orientation_only | 2087.3 | 2082.4 | 1.002x | n/a | n/a |
| identical_daily_records_changing_geometry | exponential | utc_sun | 2087.3 | 2402.4 | 0.869x | n/a | n/a |
| identical_daily_records_changing_geometry | exponential | utc_orientation | 2087.3 | 2240.8 | 0.932x | n/a | n/a |
| identical_daily_records_changing_geometry | exponential | sun_orientation | 2087.3 | 2053.3 | 1.017x | n/a | n/a |
| identical_daily_records_changing_geometry | exponential | all | 2087.3 | 2029.9 | 1.028x | n/a | n/a |
| identical_daily_records_changing_geometry | jb2008 | off_off_off | 38433.1 | 41521.8 | 0.926x | 0.0000 | 0.0000 |
| identical_daily_records_changing_geometry | jb2008 | utc_only | 38433.1 | 19094.2 | 2.013x | 1.0000 | 0.0000 |
| identical_daily_records_changing_geometry | jb2008 | sun_only | 38433.1 | 38227.3 | 1.005x | 0.0000 | 0.0000 |
| identical_daily_records_changing_geometry | jb2008 | orientation_only | 38433.1 | 40609.2 | 0.946x | 0.0000 | 0.0010 |
| identical_daily_records_changing_geometry | jb2008 | utc_sun | 38433.1 | 15753.4 | 2.440x | 1.0000 | 0.0000 |
| identical_daily_records_changing_geometry | jb2008 | utc_orientation | 38433.1 | 18261.5 | 2.105x | 1.0000 | 0.0010 |
| identical_daily_records_changing_geometry | jb2008 | sun_orientation | 38433.1 | 37827.1 | 1.016x | 0.0000 | 0.0010 |
| identical_daily_records_changing_geometry | jb2008 | all | 38433.1 | 16718.1 | 2.299x | 1.0000 | 0.0010 |
| identical_daily_records_changing_geometry | nrlmsise00 | off_off_off | 32661.3 | 34029.9 | 0.960x | 0.0000 | 0.0000 |
| identical_daily_records_changing_geometry | nrlmsise00 | utc_only | 32661.3 | 13104.9 | 2.492x | 1.0000 | 0.0000 |
| identical_daily_records_changing_geometry | nrlmsise00 | sun_only | 32661.3 | 32547.4 | 1.004x | 0.0000 | 0.0000 |
| identical_daily_records_changing_geometry | nrlmsise00 | orientation_only | 32661.3 | 34305.5 | 0.952x | 0.0000 | 0.0010 |
| identical_daily_records_changing_geometry | nrlmsise00 | utc_sun | 32661.3 | 12288.7 | 2.658x | 1.0000 | 0.0000 |
| identical_daily_records_changing_geometry | nrlmsise00 | utc_orientation | 32661.3 | 12349.2 | 2.645x | 1.0000 | 0.0010 |
| identical_daily_records_changing_geometry | nrlmsise00 | sun_orientation | 32661.3 | 36251.4 | 0.901x | 0.0000 | 0.0010 |
| identical_daily_records_changing_geometry | nrlmsise00 | all | 32661.3 | 12361.8 | 2.642x | 1.0000 | 0.0010 |
| nearby_positions_epochs | exponential | off_off_off | 2637.3 | 1920.1 | 1.374x | n/a | n/a |
| nearby_positions_epochs | exponential | utc_only | 2637.3 | 2004.3 | 1.316x | n/a | n/a |
| nearby_positions_epochs | exponential | sun_only | 2637.3 | 2046.6 | 1.289x | n/a | n/a |
| nearby_positions_epochs | exponential | orientation_only | 2637.3 | 2041.0 | 1.292x | n/a | n/a |
| nearby_positions_epochs | exponential | utc_sun | 2637.3 | 2049.8 | 1.287x | n/a | n/a |
| nearby_positions_epochs | exponential | utc_orientation | 2637.3 | 1931.6 | 1.365x | n/a | n/a |
| nearby_positions_epochs | exponential | sun_orientation | 2637.3 | 1934.2 | 1.364x | n/a | n/a |
| nearby_positions_epochs | exponential | all | 2637.3 | 1897.9 | 1.390x | n/a | n/a |
| nearby_positions_epochs | jb2008 | off_off_off | 38670.1 | 39268.2 | 0.985x | 0.0000 | 0.0000 |
| nearby_positions_epochs | jb2008 | utc_only | 38670.1 | 18057.6 | 2.141x | 1.0000 | 0.0000 |
| nearby_positions_epochs | jb2008 | sun_only | 38670.1 | 37876.8 | 1.021x | 0.0000 | 0.0000 |
| nearby_positions_epochs | jb2008 | orientation_only | 38670.1 | 39918.2 | 0.969x | 0.0000 | 0.0010 |
| nearby_positions_epochs | jb2008 | utc_sun | 38670.1 | 16432.0 | 2.353x | 1.0000 | 0.0000 |
| nearby_positions_epochs | jb2008 | utc_orientation | 38670.1 | 17916.2 | 2.158x | 1.0000 | 0.0010 |
| nearby_positions_epochs | jb2008 | sun_orientation | 38670.1 | 39426.5 | 0.981x | 0.0000 | 0.0010 |
| nearby_positions_epochs | jb2008 | all | 38670.1 | 16877.7 | 2.291x | 1.0000 | 0.0010 |
| nearby_positions_epochs | nrlmsise00 | off_off_off | 34479.4 | 33150.4 | 1.040x | 0.0000 | 0.0000 |
| nearby_positions_epochs | nrlmsise00 | utc_only | 34479.4 | 12253.2 | 2.814x | 1.0000 | 0.0000 |
| nearby_positions_epochs | nrlmsise00 | sun_only | 34479.4 | 35626.3 | 0.968x | 0.0000 | 0.0000 |
| nearby_positions_epochs | nrlmsise00 | orientation_only | 34479.4 | 33872.0 | 1.018x | 0.0000 | 0.0010 |
| nearby_positions_epochs | nrlmsise00 | utc_sun | 34479.4 | 13554.8 | 2.544x | 1.0000 | 0.0000 |
| nearby_positions_epochs | nrlmsise00 | utc_orientation | 34479.4 | 12243.7 | 2.816x | 1.0000 | 0.0010 |
| nearby_positions_epochs | nrlmsise00 | sun_orientation | 34479.4 | 37301.2 | 0.924x | 0.0000 | 0.0010 |
| nearby_positions_epochs | nrlmsise00 | all | 34479.4 | 12941.0 | 2.664x | 1.0000 | 0.0010 |
| same_epoch_changing_positions | exponential | off_off_off | 2007.3 | 2027.1 | 0.990x | n/a | n/a |
| same_epoch_changing_positions | exponential | utc_only | 2007.3 | 1922.0 | 1.044x | n/a | n/a |
| same_epoch_changing_positions | exponential | sun_only | 2007.3 | 1931.8 | 1.039x | n/a | n/a |
| same_epoch_changing_positions | exponential | orientation_only | 2007.3 | 1954.7 | 1.027x | n/a | n/a |
| same_epoch_changing_positions | exponential | utc_sun | 2007.3 | 1958.2 | 1.025x | n/a | n/a |
| same_epoch_changing_positions | exponential | utc_orientation | 2007.3 | 2031.9 | 0.988x | n/a | n/a |
| same_epoch_changing_positions | exponential | sun_orientation | 2007.3 | 1976.6 | 1.016x | n/a | n/a |
| same_epoch_changing_positions | exponential | all | 2007.3 | 1990.9 | 1.008x | n/a | n/a |
| same_epoch_changing_positions | jb2008 | off_off_off | 38247.1 | 41055.2 | 0.932x | 0.0000 | 0.0000 |
| same_epoch_changing_positions | jb2008 | utc_only | 38247.1 | 17809.9 | 2.148x | 1.0000 | 0.0000 |
| same_epoch_changing_positions | jb2008 | sun_only | 38247.1 | 38147.8 | 1.003x | 0.0000 | 0.0000 |
| same_epoch_changing_positions | jb2008 | orientation_only | 38247.1 | 39054.4 | 0.979x | 0.0000 | 1.0000 |
| same_epoch_changing_positions | jb2008 | utc_sun | 38247.1 | 17212.3 | 2.222x | 1.0000 | 0.0000 |
| same_epoch_changing_positions | jb2008 | utc_orientation | 38247.1 | 18241.2 | 2.097x | 1.0000 | 1.0000 |
| same_epoch_changing_positions | jb2008 | sun_orientation | 38247.1 | 37256.0 | 1.027x | 0.0000 | 1.0000 |
| same_epoch_changing_positions | jb2008 | all | 38247.1 | 14762.4 | 2.591x | 1.0000 | 1.0000 |
| same_epoch_changing_positions | nrlmsise00 | off_off_off | 33161.9 | 32713.9 | 1.014x | 0.0000 | 0.0000 |
| same_epoch_changing_positions | nrlmsise00 | utc_only | 33161.9 | 12059.1 | 2.750x | 1.0000 | 0.0000 |
| same_epoch_changing_positions | nrlmsise00 | sun_only | 33161.9 | 36703.9 | 0.903x | 0.0000 | 0.0000 |
| same_epoch_changing_positions | nrlmsise00 | orientation_only | 33161.9 | 30537.8 | 1.086x | 0.0000 | 1.0000 |
| same_epoch_changing_positions | nrlmsise00 | utc_sun | 33161.9 | 11937.8 | 2.778x | 1.0000 | 0.0000 |
| same_epoch_changing_positions | nrlmsise00 | utc_orientation | 33161.9 | 10109.3 | 3.280x | 1.0000 | 1.0000 |
| same_epoch_changing_positions | nrlmsise00 | sun_orientation | 33161.9 | 31729.9 | 1.045x | 0.0000 | 1.0000 |
| same_epoch_changing_positions | nrlmsise00 | all | 33161.9 | 10296.3 | 3.221x | 1.0000 | 1.0000 |
| same_position_changing_epochs | exponential | off_off_off | 2025.8 | 1990.9 | 1.018x | n/a | n/a |
| same_position_changing_epochs | exponential | utc_only | 2025.8 | 1987.6 | 1.019x | n/a | n/a |
| same_position_changing_epochs | exponential | sun_only | 2025.8 | 1973.3 | 1.027x | n/a | n/a |
| same_position_changing_epochs | exponential | orientation_only | 2025.8 | 1948.2 | 1.040x | n/a | n/a |
| same_position_changing_epochs | exponential | utc_sun | 2025.8 | 1949.0 | 1.039x | n/a | n/a |
| same_position_changing_epochs | exponential | utc_orientation | 2025.8 | 1936.2 | 1.046x | n/a | n/a |
| same_position_changing_epochs | exponential | sun_orientation | 2025.8 | 1924.6 | 1.053x | n/a | n/a |
| same_position_changing_epochs | exponential | all | 2025.8 | 1929.6 | 1.050x | n/a | n/a |
| same_position_changing_epochs | jb2008 | off_off_off | 38578.2 | 42656.0 | 0.904x | 0.0000 | 0.0000 |
| same_position_changing_epochs | jb2008 | utc_only | 38578.2 | 19766.3 | 1.952x | 1.0000 | 0.0000 |
| same_position_changing_epochs | jb2008 | sun_only | 38578.2 | 39037.7 | 0.988x | 0.0000 | 0.0000 |
| same_position_changing_epochs | jb2008 | orientation_only | 38578.2 | 41669.4 | 0.926x | 0.0000 | 0.0010 |
| same_position_changing_epochs | jb2008 | utc_sun | 38578.2 | 16871.0 | 2.287x | 1.0000 | 0.0000 |
| same_position_changing_epochs | jb2008 | utc_orientation | 38578.2 | 18571.5 | 2.077x | 1.0000 | 0.0010 |
| same_position_changing_epochs | jb2008 | sun_orientation | 38578.2 | 40353.0 | 0.956x | 0.0000 | 0.0010 |
| same_position_changing_epochs | jb2008 | all | 38578.2 | 16492.2 | 2.339x | 1.0000 | 0.0010 |
| same_position_changing_epochs | nrlmsise00 | off_off_off | 32359.0 | 33182.5 | 0.975x | 0.0000 | 0.0000 |
| same_position_changing_epochs | nrlmsise00 | utc_only | 32359.0 | 12415.6 | 2.606x | 1.0000 | 0.0000 |
| same_position_changing_epochs | nrlmsise00 | sun_only | 32359.0 | 33898.9 | 0.955x | 0.0000 | 0.0000 |
| same_position_changing_epochs | nrlmsise00 | orientation_only | 32359.0 | 33376.0 | 0.970x | 0.0000 | 0.0010 |
| same_position_changing_epochs | nrlmsise00 | utc_sun | 32359.0 | 12515.7 | 2.585x | 1.0000 | 0.0000 |
| same_position_changing_epochs | nrlmsise00 | utc_orientation | 32359.0 | 12691.8 | 2.550x | 1.0000 | 0.0010 |
| same_position_changing_epochs | nrlmsise00 | sun_orientation | 32359.0 | 34744.1 | 0.931x | 0.0000 | 0.0010 |
| same_position_changing_epochs | nrlmsise00 | all | 32359.0 | 13061.9 | 2.477x | 1.0000 | 0.0010 |
| utc_midnight | exponential | off_off_off | 2068.5 | 1841.4 | 1.123x | n/a | n/a |
| utc_midnight | exponential | utc_only | 2068.5 | 1942.7 | 1.065x | n/a | n/a |
| utc_midnight | exponential | sun_only | 2068.5 | 1836.4 | 1.126x | n/a | n/a |
| utc_midnight | exponential | orientation_only | 2068.5 | 1854.6 | 1.115x | n/a | n/a |
| utc_midnight | exponential | utc_sun | 2068.5 | 1894.9 | 1.092x | n/a | n/a |
| utc_midnight | exponential | utc_orientation | 2068.5 | 1936.0 | 1.068x | n/a | n/a |
| utc_midnight | exponential | sun_orientation | 2068.5 | 1929.6 | 1.072x | n/a | n/a |
| utc_midnight | exponential | all | 2068.5 | 1917.3 | 1.079x | n/a | n/a |
| utc_midnight | jb2008 | off_off_off | 40172.8 | 39418.0 | 1.019x | 0.0000 | 0.0000 |
| utc_midnight | jb2008 | utc_only | 40172.8 | 17995.6 | 2.232x | 0.9922 | 0.0000 |
| utc_midnight | jb2008 | sun_only | 40172.8 | 38809.6 | 1.035x | 0.0000 | 0.0000 |
| utc_midnight | jb2008 | orientation_only | 40172.8 | 40774.6 | 0.985x | 0.0000 | 0.0010 |
| utc_midnight | jb2008 | utc_sun | 40172.8 | 16720.0 | 2.403x | 0.9922 | 0.0000 |
| utc_midnight | jb2008 | utc_orientation | 40172.8 | 18175.3 | 2.210x | 0.9922 | 0.0010 |
| utc_midnight | jb2008 | sun_orientation | 40172.8 | 37423.8 | 1.073x | 0.0000 | 0.0010 |
| utc_midnight | jb2008 | all | 40172.8 | 16776.7 | 2.395x | 0.9922 | 0.0010 |
| utc_midnight | nrlmsise00 | off_off_off | 33424.4 | 33509.6 | 0.997x | 0.0000 | 0.0000 |
| utc_midnight | nrlmsise00 | utc_only | 33424.4 | 12297.9 | 2.718x | 0.9922 | 0.0000 |
| utc_midnight | nrlmsise00 | sun_only | 33424.4 | 33341.8 | 1.002x | 0.0000 | 0.0000 |
| utc_midnight | nrlmsise00 | orientation_only | 33424.4 | 34407.0 | 0.971x | 0.0000 | 0.0010 |
| utc_midnight | nrlmsise00 | utc_sun | 33424.4 | 12774.6 | 2.616x | 0.9922 | 0.0000 |
| utc_midnight | nrlmsise00 | utc_orientation | 33424.4 | 13496.1 | 2.477x | 0.9922 | 0.0010 |
| utc_midnight | nrlmsise00 | sun_orientation | 33424.4 | 34275.3 | 0.975x | 0.0000 | 0.0010 |
| utc_midnight | nrlmsise00 | all | 33424.4 | 12183.9 | 2.743x | 0.9922 | 0.0010 |
| weather_3h_boundary | exponential | off_off_off | 1985.2 | 1993.1 | 0.996x | n/a | n/a |
| weather_3h_boundary | exponential | utc_only | 1985.2 | 2060.1 | 0.964x | n/a | n/a |
| weather_3h_boundary | exponential | sun_only | 1985.2 | 2029.7 | 0.978x | n/a | n/a |
| weather_3h_boundary | exponential | orientation_only | 1985.2 | 2044.0 | 0.971x | n/a | n/a |
| weather_3h_boundary | exponential | utc_sun | 1985.2 | 2033.2 | 0.976x | n/a | n/a |
| weather_3h_boundary | exponential | utc_orientation | 1985.2 | 2059.5 | 0.964x | n/a | n/a |
| weather_3h_boundary | exponential | sun_orientation | 1985.2 | 2057.6 | 0.965x | n/a | n/a |
| weather_3h_boundary | exponential | all | 1985.2 | 2114.5 | 0.939x | n/a | n/a |
| weather_3h_boundary | jb2008 | off_off_off | 40444.1 | 40019.8 | 1.011x | 0.0000 | 0.0000 |
| weather_3h_boundary | jb2008 | utc_only | 40444.1 | 18002.2 | 2.247x | 0.9932 | 0.0000 |
| weather_3h_boundary | jb2008 | sun_only | 40444.1 | 38048.9 | 1.063x | 0.0000 | 0.0000 |
| weather_3h_boundary | jb2008 | orientation_only | 40444.1 | 39864.6 | 1.015x | 0.0000 | 0.0010 |
| weather_3h_boundary | jb2008 | utc_sun | 40444.1 | 17141.2 | 2.359x | 0.9932 | 0.0000 |
| weather_3h_boundary | jb2008 | utc_orientation | 40444.1 | 19288.1 | 2.097x | 0.9932 | 0.0010 |
| weather_3h_boundary | jb2008 | sun_orientation | 40444.1 | 38535.8 | 1.050x | 0.0000 | 0.0010 |
| weather_3h_boundary | jb2008 | all | 40444.1 | 19367.9 | 2.088x | 0.9932 | 0.0010 |
| weather_3h_boundary | nrlmsise00 | off_off_off | 35195.9 | 34732.9 | 1.013x | 0.0000 | 0.0000 |
| weather_3h_boundary | nrlmsise00 | utc_only | 35195.9 | 13366.6 | 2.633x | 0.9932 | 0.0000 |
| weather_3h_boundary | nrlmsise00 | sun_only | 35195.9 | 35135.7 | 1.002x | 0.0000 | 0.0000 |
| weather_3h_boundary | nrlmsise00 | orientation_only | 35195.9 | 40755.8 | 0.864x | 0.0000 | 0.0010 |
| weather_3h_boundary | nrlmsise00 | utc_sun | 35195.9 | 13388.9 | 2.629x | 0.9932 | 0.0000 |
| weather_3h_boundary | nrlmsise00 | utc_orientation | 35195.9 | 12958.4 | 2.716x | 0.9932 | 0.0010 |
| weather_3h_boundary | nrlmsise00 | sun_orientation | 35195.9 | 35434.1 | 0.993x | 0.0000 | 0.0010 |
| weather_3h_boundary | nrlmsise00 | all | 35195.9 | 12656.8 | 2.781x | 0.9932 | 0.0010 |
| weather_hour_boundary | exponential | off_off_off | 2316.3 | 2110.0 | 1.098x | n/a | n/a |
| weather_hour_boundary | exponential | utc_only | 2316.3 | 2364.2 | 0.980x | n/a | n/a |
| weather_hour_boundary | exponential | sun_only | 2316.3 | 2979.0 | 0.778x | n/a | n/a |
| weather_hour_boundary | exponential | orientation_only | 2316.3 | 2185.9 | 1.060x | n/a | n/a |
| weather_hour_boundary | exponential | utc_sun | 2316.3 | 2346.6 | 0.987x | n/a | n/a |
| weather_hour_boundary | exponential | utc_orientation | 2316.3 | 2243.0 | 1.033x | n/a | n/a |
| weather_hour_boundary | exponential | sun_orientation | 2316.3 | 2098.1 | 1.104x | n/a | n/a |
| weather_hour_boundary | exponential | all | 2316.3 | 2083.9 | 1.112x | n/a | n/a |
| weather_hour_boundary | jb2008 | off_off_off | 42535.3 | 40887.6 | 1.040x | 0.0000 | 0.0000 |
| weather_hour_boundary | jb2008 | utc_only | 42535.3 | 19664.1 | 2.163x | 0.9932 | 0.0000 |
| weather_hour_boundary | jb2008 | sun_only | 42535.3 | 39272.3 | 1.083x | 0.0000 | 0.0000 |
| weather_hour_boundary | jb2008 | orientation_only | 42535.3 | 40068.7 | 1.062x | 0.0000 | 0.0010 |
| weather_hour_boundary | jb2008 | utc_sun | 42535.3 | 16575.6 | 2.566x | 0.9932 | 0.0000 |
| weather_hour_boundary | jb2008 | utc_orientation | 42535.3 | 18137.3 | 2.345x | 0.9932 | 0.0010 |
| weather_hour_boundary | jb2008 | sun_orientation | 42535.3 | 37796.9 | 1.125x | 0.0000 | 0.0010 |
| weather_hour_boundary | jb2008 | all | 42535.3 | 15879.3 | 2.679x | 0.9932 | 0.0010 |
| weather_hour_boundary | nrlmsise00 | off_off_off | 33243.3 | 36634.2 | 0.907x | 0.0000 | 0.0000 |
| weather_hour_boundary | nrlmsise00 | utc_only | 33243.3 | 12595.2 | 2.639x | 0.9932 | 0.0000 |
| weather_hour_boundary | nrlmsise00 | sun_only | 33243.3 | 33555.6 | 0.991x | 0.0000 | 0.0000 |
| weather_hour_boundary | nrlmsise00 | orientation_only | 33243.3 | 33591.4 | 0.990x | 0.0000 | 0.0010 |
| weather_hour_boundary | nrlmsise00 | utc_sun | 33243.3 | 12712.9 | 2.615x | 0.9932 | 0.0000 |
| weather_hour_boundary | nrlmsise00 | utc_orientation | 33243.3 | 12945.0 | 2.568x | 0.9932 | 0.0010 |
| weather_hour_boundary | nrlmsise00 | sun_orientation | 33243.3 | 36642.4 | 0.907x | 0.0000 | 0.0010 |
| weather_hour_boundary | nrlmsise00 | all | 33243.3 | 13005.2 | 2.556x | 0.9932 | 0.0010 |

## Replay versus measured hit rates

| schedule | model | simulated UTC hit rate | measured UTC hit rate | difference |
|---|---|---:|---:|---:|
| fixed | jb2008 | 1.0000 | 1.0000 | 0.0000 |
| fixed | nrlmsise00 | 1.0000 | 1.0000 | 0.0000 |
| same_epoch_changing_positions | jb2008 | 1.0000 | 1.0000 | 0.0000 |
| same_epoch_changing_positions | nrlmsise00 | 1.0000 | 1.0000 | 0.0000 |
| same_position_changing_epochs | jb2008 | 1.0000 | 1.0000 | 0.0000 |
| same_position_changing_epochs | nrlmsise00 | 1.0000 | 1.0000 | 0.0000 |
| nearby_positions_epochs | jb2008 | 1.0000 | 1.0000 | 0.0000 |
| nearby_positions_epochs | nrlmsise00 | 1.0000 | 1.0000 | 0.0000 |
| identical_daily_records_changing_geometry | jb2008 | 1.0000 | 1.0000 | 0.0000 |
| identical_daily_records_changing_geometry | nrlmsise00 | 1.0000 | 1.0000 | 0.0000 |
| weather_3h_boundary | jb2008 | 0.9932 | 0.9932 | 0.0000 |
| weather_3h_boundary | nrlmsise00 | 0.9932 | 0.9932 | 0.0000 |
| weather_hour_boundary | jb2008 | 0.9932 | 0.9932 | 0.0000 |
| weather_hour_boundary | nrlmsise00 | 0.9932 | 0.9932 | 0.0000 |
| utc_midnight | jb2008 | 0.9922 | 0.9922 | 0.0000 |
| utc_midnight | nrlmsise00 | 0.9922 | 0.9922 | 0.0000 |
| ensemble_trajectory_major | jb2008 | 1.0000 | 1.0000 | 0.0000 |
| ensemble_trajectory_major | nrlmsise00 | 1.0000 | 1.0000 | 0.0000 |
| ensemble_epoch_major | jb2008 | 1.0000 | 1.0000 | 0.0000 |
| ensemble_epoch_major | nrlmsise00 | 1.0000 | 1.0000 | 0.0000 |

## Shared cache versus thread-local prototype

```json
{
  "replay": [
    {
      "schedule": "fixed",
      "threads": 1,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1024,
      "shared_orientation_misses": 0,
      "thread_local_orientation_hits": 1024,
      "thread_local_orientation_misses": 0,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "fixed",
      "threads": 2,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1024,
      "shared_orientation_misses": 0,
      "thread_local_orientation_hits": 1024,
      "thread_local_orientation_misses": 0,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "fixed",
      "threads": 4,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1024,
      "shared_orientation_misses": 0,
      "thread_local_orientation_hits": 1024,
      "thread_local_orientation_misses": 0,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "same_epoch_changing_positions",
      "threads": 1,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1024,
      "shared_orientation_misses": 0,
      "thread_local_orientation_hits": 1024,
      "thread_local_orientation_misses": 0,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "same_epoch_changing_positions",
      "threads": 2,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1024,
      "shared_orientation_misses": 0,
      "thread_local_orientation_hits": 1024,
      "thread_local_orientation_misses": 0,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "same_epoch_changing_positions",
      "threads": 4,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1024,
      "shared_orientation_misses": 0,
      "thread_local_orientation_hits": 1024,
      "thread_local_orientation_misses": 0,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "same_position_changing_epochs",
      "threads": 1,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 1,
      "thread_local_orientation_misses": 1023,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "same_position_changing_epochs",
      "threads": 2,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 2,
      "thread_local_orientation_misses": 1022,
      "orientation_miss_delta_thread_local_minus_shared": -1
    },
    {
      "schedule": "same_position_changing_epochs",
      "threads": 4,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 4,
      "thread_local_orientation_misses": 1020,
      "orientation_miss_delta_thread_local_minus_shared": -3
    },
    {
      "schedule": "nearby_positions_epochs",
      "threads": 1,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 1,
      "thread_local_orientation_misses": 1023,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "nearby_positions_epochs",
      "threads": 2,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 2,
      "thread_local_orientation_misses": 1022,
      "orientation_miss_delta_thread_local_minus_shared": -1
    },
    {
      "schedule": "nearby_positions_epochs",
      "threads": 4,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 4,
      "thread_local_orientation_misses": 1020,
      "orientation_miss_delta_thread_local_minus_shared": -3
    },
    {
      "schedule": "identical_daily_records_changing_geometry",
      "threads": 1,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 1,
      "thread_local_orientation_misses": 1023,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "identical_daily_records_changing_geometry",
      "threads": 2,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 2,
      "thread_local_orientation_misses": 1022,
      "orientation_miss_delta_thread_local_minus_shared": -1
    },
    {
      "schedule": "identical_daily_records_changing_geometry",
      "threads": 4,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 4,
      "thread_local_orientation_misses": 1020,
      "orientation_miss_delta_thread_local_minus_shared": -3
    },
    {
      "schedule": "weather_3h_boundary",
      "threads": 1,
      "shared_utc_hits": 1017,
      "shared_utc_misses": 7,
      "thread_local_utc_hits": 1017,
      "thread_local_utc_misses": 7,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 1,
      "thread_local_orientation_misses": 1023,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "weather_3h_boundary",
      "threads": 2,
      "shared_utc_hits": 1017,
      "shared_utc_misses": 7,
      "thread_local_utc_hits": 1010,
      "thread_local_utc_misses": 14,
      "utc_miss_delta_thread_local_minus_shared": 7,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 2,
      "thread_local_orientation_misses": 1022,
      "orientation_miss_delta_thread_local_minus_shared": -1
    },
    {
      "schedule": "weather_3h_boundary",
      "threads": 4,
      "shared_utc_hits": 1017,
      "shared_utc_misses": 7,
      "thread_local_utc_hits": 996,
      "thread_local_utc_misses": 28,
      "utc_miss_delta_thread_local_minus_shared": 21,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 4,
      "thread_local_orientation_misses": 1020,
      "orientation_miss_delta_thread_local_minus_shared": -3
    },
    {
      "schedule": "weather_hour_boundary",
      "threads": 1,
      "shared_utc_hits": 1017,
      "shared_utc_misses": 7,
      "thread_local_utc_hits": 1017,
      "thread_local_utc_misses": 7,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 1,
      "thread_local_orientation_misses": 1023,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "weather_hour_boundary",
      "threads": 2,
      "shared_utc_hits": 1017,
      "shared_utc_misses": 7,
      "thread_local_utc_hits": 1010,
      "thread_local_utc_misses": 14,
      "utc_miss_delta_thread_local_minus_shared": 7,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 2,
      "thread_local_orientation_misses": 1022,
      "orientation_miss_delta_thread_local_minus_shared": -1
    },
    {
      "schedule": "weather_hour_boundary",
      "threads": 4,
      "shared_utc_hits": 1017,
      "shared_utc_misses": 7,
      "thread_local_utc_hits": 996,
      "thread_local_utc_misses": 28,
      "utc_miss_delta_thread_local_minus_shared": 21,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 4,
      "thread_local_orientation_misses": 1020,
      "orientation_miss_delta_thread_local_minus_shared": -3
    },
    {
      "schedule": "utc_midnight",
      "threads": 1,
      "shared_utc_hits": 1016,
      "shared_utc_misses": 8,
      "thread_local_utc_hits": 1016,
      "thread_local_utc_misses": 8,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 1,
      "thread_local_orientation_misses": 1023,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "utc_midnight",
      "threads": 2,
      "shared_utc_hits": 1016,
      "shared_utc_misses": 8,
      "thread_local_utc_hits": 1008,
      "thread_local_utc_misses": 16,
      "utc_miss_delta_thread_local_minus_shared": 8,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 2,
      "thread_local_orientation_misses": 1022,
      "orientation_miss_delta_thread_local_minus_shared": -1
    },
    {
      "schedule": "utc_midnight",
      "threads": 4,
      "shared_utc_hits": 1016,
      "shared_utc_misses": 8,
      "thread_local_utc_hits": 992,
      "thread_local_utc_misses": 32,
      "utc_miss_delta_thread_local_minus_shared": 24,
      "shared_orientation_hits": 1,
      "shared_orientation_misses": 1023,
      "thread_local_orientation_hits": 4,
      "thread_local_orientation_misses": 1020,
      "orientation_miss_delta_thread_local_minus_shared": -3
    },
    {
      "schedule": "ensemble_trajectory_major",
      "threads": 1,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 757,
      "shared_orientation_misses": 267,
      "thread_local_orientation_hits": 757,
      "thread_local_orientation_misses": 267,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "ensemble_trajectory_major",
      "threads": 2,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 757,
      "shared_orientation_misses": 267,
      "thread_local_orientation_hits": 828,
      "thread_local_orientation_misses": 196,
      "orientation_miss_delta_thread_local_minus_shared": -71
    },
    {
      "schedule": "ensemble_trajectory_major",
      "threads": 4,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 757,
      "shared_orientation_misses": 267,
      "thread_local_orientation_hits": 823,
      "thread_local_orientation_misses": 201,
      "orientation_miss_delta_thread_local_minus_shared": -66
    },
    {
      "schedule": "ensemble_epoch_major",
      "threads": 1,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 897,
      "shared_orientation_misses": 127,
      "thread_local_orientation_hits": 897,
      "thread_local_orientation_misses": 127,
      "orientation_miss_delta_thread_local_minus_shared": 0
    },
    {
      "schedule": "ensemble_epoch_major",
      "threads": 2,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 897,
      "shared_orientation_misses": 127,
      "thread_local_orientation_hits": 770,
      "thread_local_orientation_misses": 254,
      "orientation_miss_delta_thread_local_minus_shared": 127
    },
    {
      "schedule": "ensemble_epoch_major",
      "threads": 4,
      "shared_utc_hits": 1024,
      "shared_utc_misses": 0,
      "thread_local_utc_hits": 1024,
      "thread_local_utc_misses": 0,
      "utc_miss_delta_thread_local_minus_shared": 0,
      "shared_orientation_hits": 897,
      "shared_orientation_misses": 127,
      "thread_local_orientation_hits": 516,
      "thread_local_orientation_misses": 508,
      "orientation_miss_delta_thread_local_minus_shared": 381
    }
  ],
  "threads": [
    {
      "threads": 1,
      "replayed_extra_utc_misses_thread_local": 0,
      "replayed_extra_orientation_misses_thread_local": 0,
      "rows": [
        {
          "model": "exponential",
          "contended_fraction": null,
          "mutex_wait_ns_per_acquisition": null,
          "throughput_ratio_all_over_control": 1.018008147468788
        },
        {
          "model": "jb2008",
          "contended_fraction": 0.0,
          "mutex_wait_ns_per_acquisition": 30.028975,
          "throughput_ratio_all_over_control": 3.056906082384579
        },
        {
          "model": "nrlmsise00",
          "contended_fraction": 0.0,
          "mutex_wait_ns_per_acquisition": 29.753933333333332,
          "throughput_ratio_all_over_control": 4.202991659830488
        }
      ]
    },
    {
      "threads": 2,
      "replayed_extra_utc_misses_thread_local": 22,
      "replayed_extra_orientation_misses_thread_local": 50,
      "rows": [
        {
          "model": "exponential",
          "contended_fraction": null,
          "mutex_wait_ns_per_acquisition": null,
          "throughput_ratio_all_over_control": 0.9512100303142234
        },
        {
          "model": "jb2008",
          "contended_fraction": 0.0526875,
          "mutex_wait_ns_per_acquisition": 1891.0895125,
          "throughput_ratio_all_over_control": 2.3270757495295027
        },
        {
          "model": "nrlmsise00",
          "contended_fraction": 0.05511666666666667,
          "mutex_wait_ns_per_acquisition": 8768.712766666667,
          "throughput_ratio_all_over_control": 2.9927804050321147
        }
      ]
    },
    {
      "threads": 4,
      "replayed_extra_utc_misses_thread_local": 66,
      "replayed_extra_orientation_misses_thread_local": 297,
      "rows": [
        {
          "model": "exponential",
          "contended_fraction": null,
          "mutex_wait_ns_per_acquisition": null,
          "throughput_ratio_all_over_control": 1.0357990952210745
        },
        {
          "model": "jb2008",
          "contended_fraction": 0.1426875,
          "mutex_wait_ns_per_acquisition": 6857.10991875,
          "throughput_ratio_all_over_control": 2.89596542169103
        },
        {
          "model": "nrlmsise00",
          "contended_fraction": 0.11473333333333334,
          "mutex_wait_ns_per_acquisition": 17494.495316666667,
          "throughput_ratio_all_over_control": 3.9648644953405037
        }
      ]
    }
  ]
}
```

## Invalidation

```json
{
  "results_identical_across_invalidation": true,
  "cross_provider_compatible_sun": true,
  "wrong_epoch_compatible_sun": false,
  "invalidate_reuse_ns_mean": 101.636,
  "invalidate_reuse_ns_p99": 110
}
```

## Scientific verification

```json
{
  "provider_bitwise_equal_to_original": true,
  "locality_bitwise_equal_to_original": true,
  "concurrency_bitwise_equal_to_original": true,
  "propagate_bitwise_equal_to_original": true,
  "original_diagnostics": {
    "provider_bitwise_equal_to_original": true,
    "locality_bitwise_equal_to_original": true,
    "concurrency_bitwise_equal_to_original": true,
    "propagate_bitwise_equal_to_original": true,
    "propagate_checksums": {
      "CASE-A_exponential": "c62559158595e38b",
      "CASE-A_jb2008": "c4efb98e5a5d53b1",
      "CASE-A_nrlmsise00": "349cb3a2623b7fb4",
      "CASE-B_exponential": "ef137c6c71673689",
      "CASE-B_jb2008": "abc7208a5238c164",
      "CASE-B_nrlmsise00": "a448f2a488cfe78",
      "CASE-C_exponential": "5b85591bde790979",
      "CASE-C_jb2008": "df04599b172ddc9c",
      "CASE-C_nrlmsise00": "b83356ec9d7c4ed2"
    }
  },
  "invalidation": {
    "results_identical_across_invalidation": true,
    "cross_provider_compatible_sun": true,
    "wrong_epoch_compatible_sun": false,
    "invalidate_reuse_ns_mean": 101.636,
    "invalidate_reuse_ns_p99": 110
  },
  "preserved_evidence_unchanged": true,
  "profiling_unchanged": true,
  "production_sources_unchanged": true
}
```
