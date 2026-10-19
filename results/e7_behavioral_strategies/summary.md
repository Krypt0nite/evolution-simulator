# e7_behavioral_strategies

Utility controller with evolvable weights vs utility controller with weights frozen at founder values vs an evolved neural-network controller (random initial weights).

**Question:** Under identical ecology and seeds, do different decision architectures differ in population size, foraging efficiency and the traits they evolve?

Runs: 10 seeds × 3 conditions, 15000 ticks each. Unit of replication = one independent simulation run. Values are mean ± SD [95% t-CI]; d = Cohen's d vs. `utility_evolving`; p = Welch's t-test.

| metric | utility_evolving | utility_frozen_weights | neural |
|---|---|---|---|
| extinct | 0/10 [0.00, 0.28] | 0/10 [0.00, 0.28] | 0/10 [0.00, 0.28] |
| mean_population | 484 ± 64 [438, 530] | 543 ± 92 [477, 609]; d=0.74, p=0.117 | 480 ± 113 [399, 560]; d=-0.05, p=0.910 |
| mean_lifespan | 896 ± 50 [860, 932] | 945 ± 60 [902, 988]; d=0.87, p=0.067 | 677 ± 66 [629, 724]; d=-3.74, p<0.001 |
| reproductive_success | 0.518 ± 0.017 [0.506, 0.530] | 0.515 ± 0.021 [0.500, 0.530]; d=-0.14, p=0.766 | 0.452 ± 0.040 [0.423, 0.480]; d=-2.16, p<0.001 |
| mean_diversity | 0.102 ± 0.011 [0.095, 0.110] | 0.077 ± 0.009 [0.070, 0.083]; d=-2.56, p<0.001 | 0.085 ± 0.011 [0.078, 0.093]; d=-1.60, p=0.002 |
| max_generation | 36.800 ± 4.264 [33.750, 39.850] | 35.600 ± 4.766 [32.191, 39.009]; d=-0.27, p=0.560 | 47.300 ± 4.809 [43.860, 50.740]; d=2.31, p<0.001 |
| energy_capture | 0.634 ± 0.045 [0.602, 0.667] | 0.677 ± 0.073 [0.625, 0.730]; d=0.71, p=0.133 | 0.627 ± 0.067 [0.579, 0.675]; d=-0.13, p=0.777 |
| final_speed | 2.717 ± 0.576 [2.305, 3.129] | 2.550 ± 0.319 [2.322, 2.778]; d=-0.36, p=0.435 | 2.476 ± 0.517 [2.106, 2.845]; d=-0.44, p=0.337 |
| final_vision | 12.782 ± 0.998 [12.069, 13.496] | 13.148 ± 1.196 [12.293, 14.004]; d=0.33, p=0.467 | 22.789 ± 5.272 [19.018, 26.559]; d=2.64, p<0.001 |
| final_size | 0.661 ± 0.115 [0.578, 0.743] | 0.669 ± 0.120 [0.583, 0.754]; d=0.07, p=0.885 | 0.831 ± 0.114 [0.750, 0.913]; d=1.49, p=0.004 |
| final_metabolism | 0.817 ± 0.246 [0.641, 0.992] | 0.777 ± 0.150 [0.670, 0.884]; d=-0.20, p=0.668 | 0.940 ± 0.258 [0.755, 1.124]; d=0.49, p=0.289 |
| final_maturity | 142 ± 30 [120, 164] | 111 ± 35 [85, 136]; d=-0.96, p=0.047 | 131 ± 28 [110, 151]; d=-0.38, p=0.403 |
| final_fertility | 0.231 ± 0.115 [0.149, 0.314] | 0.225 ± 0.060 [0.183, 0.268]; d=-0.06, p=0.889 | 0.186 ± 0.078 [0.130, 0.242]; d=-0.46, p=0.319 |
| final_investment | 0.350 ± 0.033 [0.326, 0.373] | 0.351 ± 0.042 [0.320, 0.381]; d=0.03, p=0.946 | 0.351 ± 0.040 [0.322, 0.380]; d=0.04, p=0.930 |
| final_mutationRate | 0.117 ± 0.073 [0.064, 0.169] | 0.064 ± 0.019 [0.050, 0.078]; d=-0.98, p=0.052 | 0.062 ± 0.015 [0.051, 0.073]; d=-1.04, p=0.044 |
| final_social | -0.197 ± 0.253 [-0.377, -0.016] | 0.036 ± 0.125 [-0.053, 0.126]; d=1.17, p=0.021 | -0.101 ± 0.297 [-0.313, 0.111]; d=0.35, p=0.446 |
| final_riskTolerance | 0.529 ± 0.108 [0.451, 0.606] | 0.467 ± 0.123 [0.379, 0.554]; d=-0.54, p=0.246 | 0.569 ± 0.077 [0.514, 0.624]; d=0.43, p=0.348 |
| final_thermalOptimum | -0.021 ± 0.102 [-0.094, 0.052] | 0.028 ± 0.062 [-0.016, 0.072]; d=0.58, p=0.216 | 0.024 ± 0.075 [-0.029, 0.078]; d=0.50, p=0.276 |

Raw per-run data: `runs.csv`; per-sample trajectories: `timeseries.csv`; aggregated trajectories with 95% CIs: `timeseries_summary.csv`.
