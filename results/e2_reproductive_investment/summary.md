# e2_reproductive_investment

Maturity age fixed early (30-40 ticks) vs fixed late (250-300 ticks), plus a mixed population where both founder types compete and maturity can evolve.

**Question:** Under resource limitation, does early reproduction or delayed maturity yield larger, more persistent populations — and which way does maturity evolve when both strategies compete?

Runs: 10 seeds × 3 conditions, 15000 ticks each. Unit of replication = one independent simulation run. Values are mean ± SD [95% t-CI]; d = Cohen's d vs. `early_fixed`; p = Welch's t-test.

| metric | early_fixed | late_fixed | mixed_evolving |
|---|---|---|---|
| extinct | 0/10 [0.00, 0.28] | 0/10 [0.00, 0.28] | 0/10 [0.00, 0.28] |
| mean_population | 468 ± 61 [424, 511] | 454 ± 73 [401, 506]; d=-0.21, p=0.645 | 470 ± 63 [424, 515]; d=0.03, p=0.947 |
| mean_lifespan | 901 ± 52 [864, 939] | 955 ± 67 [907, 1003]; d=0.90, p=0.061 | 920 ± 50 [884, 956]; d=0.37, p=0.419 |
| reproductive_success | 0.516 ± 0.020 [0.502, 0.530] | 0.510 ± 0.021 [0.495, 0.525]; d=-0.29, p=0.519 | 0.526 ± 0.023 [0.510, 0.542]; d=0.48, p=0.295 |
| mean_diversity | 0.097 ± 0.011 [0.090, 0.105] | 0.094 ± 0.010 [0.086, 0.101]; d=-0.35, p=0.438 | 0.087 ± 0.013 [0.078, 0.096]; d=-0.90, p=0.059 |
| max_generation | 37.300 ± 2.452 [35.546, 39.054] | 31.800 ± 4.022 [28.923, 34.677]; d=-1.65, p=0.002 | 40.200 ± 5.770 [36.073, 44.327]; d=0.65, p=0.169 |
| energy_capture | 0.649 ± 0.048 [0.614, 0.683] | 0.626 ± 0.049 [0.590, 0.661]; d=-0.47, p=0.309 | 0.681 ± 0.092 [0.615, 0.747]; d=0.44, p=0.342 |
| final_speed | 2.919 ± 0.548 [2.527, 3.311] | 2.619 ± 0.566 [2.214, 3.023]; d=-0.54, p=0.244 | 2.856 ± 0.431 [2.548, 3.164]; d=-0.13, p=0.780 |
| final_vision | 13.096 ± 0.886 [12.462, 13.729] | 13.042 ± 1.024 [12.309, 13.774]; d=-0.06, p=0.901 | 12.940 ± 1.164 [12.108, 13.773]; d=-0.15, p=0.741 |
| final_size | 0.678 ± 0.172 [0.555, 0.801] | 0.694 ± 0.103 [0.620, 0.767]; d=0.11, p=0.809 | 0.750 ± 0.190 [0.614, 0.885]; d=0.40, p=0.388 |
| final_metabolism | 0.847 ± 0.134 [0.751, 0.942] | 0.832 ± 0.155 [0.721, 0.943]; d=-0.10, p=0.821 | 0.772 ± 0.173 [0.648, 0.896]; d=-0.48, p=0.295 |
| final_maturity | 34.616 ± 1.886 [33.268, 35.965] | 275 ± 14 [265, 285]; d=23.73, p<0.001 | 93.309 ± 82.572 [34.245, 152.373]; d=1.00, p=0.051 |
| final_fertility | 0.199 ± 0.035 [0.174, 0.224] | 0.218 ± 0.065 [0.171, 0.264]; d=0.36, p=0.433 | 0.193 ± 0.072 [0.141, 0.244]; d=-0.11, p=0.811 |
| final_investment | 0.356 ± 0.032 [0.333, 0.379] | 0.356 ± 0.056 [0.316, 0.396]; d=-0.00, p=0.996 | 0.381 ± 0.036 [0.355, 0.407]; d=0.73, p=0.118 |
| final_mutationRate | 0.088 ± 0.058 [0.046, 0.129] | 0.062 ± 0.012 [0.054, 0.071]; d=-0.60, p=0.210 | 0.056 ± 0.011 [0.049, 0.064]; d=-0.75, p=0.126 |
| final_social | 0.004 ± 0.268 [-0.188, 0.196] | -0.027 ± 0.176 [-0.152, 0.099]; d=-0.14, p=0.766 | 0.061 ± 0.164 [-0.057, 0.178]; d=0.26, p=0.577 |
| final_riskTolerance | 0.529 ± 0.128 [0.438, 0.621] | 0.510 ± 0.109 [0.433, 0.588]; d=-0.16, p=0.725 | 0.559 ± 0.128 [0.468, 0.651]; d=0.24, p=0.605 |
| final_thermalOptimum | 0.006 ± 0.099 [-0.065, 0.076] | -0.007 ± 0.078 [-0.063, 0.049]; d=-0.14, p=0.755 | 0.097 ± 0.079 [0.041, 0.154]; d=1.02, p=0.035 |

Raw per-run data: `runs.csv`; per-sample trajectories: `timeseries.csv`; aggregated trajectories with 95% CIs: `timeseries_summary.csv`.
