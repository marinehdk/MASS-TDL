# COLAV-Simulator Capability Inventory for C++ Reference Implementation

**Date:** 2026-05-11
**Author:** marinehdk
**Purpose:** Requirements analysis for C++ reference implementation of maritime COLAV simulation framework
**Source:** CCTA2023 paper + colav-simulator codebase (commit 48bffa6)

---

## 1. Overview

The colav-simulator is a Python-based maritime collision avoidance (COLAV) simulation framework supporting:
- ENC chart integration for anti-grounding
- Multi-ship COLAV algorithm testing
- Gymnasium-compatible RL environment
- Scenario generation and batch execution

This document maps every capability described in the paper to the Python implementation, serving as a **complete requirements checklist** for a C++ reference implementation.

---

## 2. Environmental Model

### 2.1 Electronic Navigational Charts (ENC)

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| ENC chart loading (seacharts) | §2.1.1 | `simulator.py`, `map_functions.py` | High |
| Grounding hazard detection | §2.1.1 | `map_functions.py` - `check_if_pointing_too_close_towards_land()` | High |
| Anti-grounding detection | §2.1.1 | `map_functions.py` | High |
| Land polygon distance computation | §2.1.1 | `map_functions.py` | High |
| Shore boundary handling | §2.1.1 | `map_functions.py` | High |
| Bounding box from ENC data | §2.1.1 | `map_functions.py` - `create_bbox_from_points()` | High |
| UTM coordinate system (zone 32/33) | §2.1.1 | `map_functions.py` - `local2latlon()` | High |
| Geodetic transformations (osr, geopy) | §2.1.1 | `map_functions.py` | Medium |
| Shapely geometry support (polygons, points) | §2.1.1 | Throughout | High |
| GeoPandas integration | §2.1.1 | `map_functions.py` | Medium |

### 2.2 Disturbance Models

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| Gauss-Markov wind model | §2.1.2 | `stochasticity.py` - `GaussMarkovDisturbanceParams` | High |
| Gauss-Markov current model | §2.1.2 | `stochasticity.py` | High |
| Constant wind/current mode | §2.1.2 | `stochasticity.py` - `constant` param | High |
| Impulse noise (wind + current) | §2.1.2 | `stochasticity.py` - `add_impulse_noise`, `speed_impulses`, `direction_impulses` | Medium |
| Moving average filter | §2.1.2 | `stochasticity.py` - `MovingAverageFilter` class | Medium |
| Monte Carlo simulation support | §2.1.2 | `stochasticity.py` | Medium |

---

## 3. Vessel Model

### 3.1 Ship Motion Models

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| KinematicCSOG model | §2.2.1 | `models.py` - `KinematicCSOGParams` | High |
| Dynamic vessel model (Viknes 3-DOF) | §2.2.1 | `models.py` - `ViknesParams` | High |
| 3-DOF state [x, y, psi, u, v, r]^T | §2.2.1 | `models.py` | High |
| Mass matrix M_rb (rigid body) | §2.2.1 | `models.py` - `ViknesParams.M_rb` | High |
| Added mass matrix M_a | §2.2.1 | `models.py` | High |
| Cubic damping D_c | §2.2.1 | `models.py` | High |
| Quadratic damping D_q | §2.2.1 | `models.py` | High |
| Linear damping D_l | §2.2.1 | `models.py` | High |
| Wind force computation (A_Fw, A_Lw) | §2.2.1 | `models.py` - `ViknesParams` | Medium |
| Drag coefficients (CD_l_AF_0, CD_l_AF_pi, CD_t) | §2.2.1 | `models.py` | Medium |
| Turn rate limit r_max | §2.2.1 | `models.py` | High |
| Speed limits (U_min, U_max) | §2.2.1 | `models.py` | High |
| Force limits (Fx_limits, Fy_limits) | §2.2.1 | `models.py` - `ViknesParams.Fx_limits`, `Fy_limits` | High |
| Ship vertex geometry generation | §2.2.1 | `models.py` | Medium |
| Draft specification | §2.2.1 | `models.py` | Medium |

### 3.2 Propeller and Rudder Dynamics

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| Rudder-propeller mapping | §2.2.2 | `controllers.py` - `PassThroughInputsParams` | High |
| Thrust force mapping | §2.2.2 | `models.py` | High |

---

## 4. Sensor Model

### 4.1 AIS (Automatic Identification System)

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| AIS transponder simulation (Class A/B) | §2.3.1 | `sensing.py` - `AISClass` enum | High |
| AIS data parsing (MMSI, SOG, COG) | §2.3.1 | `sensing.py` | High |
| Ship state init from AIS data | §2.3.1 | `ship.py` - `Config.csog_state` | High |
| AIS state format [x, y, SOG, COG] | §2.3.1 | `ship.py` - `csog_state` | High |
| Historical AIS data import | §2.3.1 + §4 | `vessel_data.py`, `data/ais/` | High |

### 4.2 Radar

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| Radar sensor model | §2.3.2 | `sensing.py` - `RadarParams` | High |
| Max range configuration | §2.3.2 | `sensing.py` - `max_range` | High |
| Measurement rate (Hz) | §2.3.2 | `sensing.py` - `measurement_rate` | High |
| Measurement noise covariance (NE) R_ne | §2.3.2 | `sensing.py` - `R_ne` | High |
| True noise characteristics R_ne_true | §2.3.2 | `sensing.py` | Medium |
| Clutter generation | §2.3.2 | `sensing.py` - `generate_clutter`, `clutter_cardinality_expectation` | High |
| Detection probability | §2.3.2 | `sensing.py` - `detection_probability` | High |
| Polar coordinate noise R_polar_true | §2.3.2 | `sensing.py` - `R_polar_true`, `include_polar_meas_noise` | Medium |

---

## 5. Tracking Model

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| Kalman Filter (KF) tracker | §2.4.1 | `trackers.py` - `KF` class | High |
| Initial covariance P_0 | §2.4.1 | `trackers.py` - `KFParams.P_0` | High |
| Process noise q | §2.4.1 | `trackers.py` - `KFParams.q` | High |
| Track sorting by distance | §2.4.1 | `trackers.py` | Medium |
| God Tracker (perfect knowledge) | §2.4.2 | `trackers.py` - `GodTracker` class | High |
| JPDA tracker (vimmjipda library) | §2.4.3 | External `vimmjipda` | High |
| MHT tracker support | §2.4.3 | External `vimmjipda` | Medium |

---

## 6. Guidance System

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| LOS guidance law | §2.5.1 | `guidances.py` - `LOSGuidanceParams` | High |
| Lookahead distance (K_p = 1/lookahead) | §2.5.1 | `guidances.py` - `K_p` | High |
| Integral action K_i for cross-track error | §2.5.1 | `guidances.py` - `K_i` | High |
| Pass angle threshold (waypoint switching) | §2.5.1 | `guidances.py` - `pass_angle_threshold` | High |
| Radius of acceptance R_a | §2.5.1 | `guidances.py` - `R_a` | High |
| Max cross-track error integration | §2.5.1 | `guidances.py` - `max_cross_track_error_int` | High |
| Cross-track error integration threshold | §2.5.1 | `guidances.py` - `cross_track_error_int_threshold` | High |
| Keep-to-path (KTP) guidance | §2.5.2 | `guidances.py` - `ktp` option | Medium |

---

## 7. Control System

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| MIMO PID controller (3-DOF) | §2.6.1 | `controllers.py` - `MIMOPIDParams` | High |
| SHPID controller (feedback lin. + PID) | §2.6.1 | `controllers.py` - `SHPIDParams` | High |
| Natural frequency wn | §2.6.1 | `controllers.py` | High |
| Damping ratio zeta | §2.6.1 | `controllers.py` | High |
| Rate limiting (eta_diff_max, z_diff_max) | §2.6.1 | `controllers.py` | Medium |
| FLSC (feedback linearizing surge-course) | §2.6.2 | `controllers.py` - `FLSCParams` | High |
| Speed P gain K_p_u | §2.6.2 | `controllers.py` | High |
| Speed I gain K_i_u | §2.6.2 | `controllers.py` | High |
| Course P gain K_p_chi | §2.6.2 | `controllers.py` | High |
| Course D gain K_d_chi | §2.6.2 | `controllers.py` | High |
| Course I gain K_i_chi | §2.6.2 | `controllers.py` | High |
| Speed error integral limit | §2.6.2 | `controllers.py` - `max_speed_error_int` | Medium |
| Course error integral limit | §2.6.2 | `controllers.py` - `max_chi_error_int` | Medium |
| Pass-through course controller | §2.6.3 | `controllers.py` - `pass_through_cs` | High |
| Pass-through inputs (rudder/propeller) | §2.6.3 | `controllers.py` - `PassThroughInputsParams` | High |

---

## 8. COLAV Algorithms

### 8.1 Implemented Algorithms

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| Velocity Obstacle (Kuwata VO) | §2.7.1 | `colav/kuwata_vo_alg/kuwata_vo.py` | High |
| VO collision cone computation | §2.7.1 | `kuwata_vo.py` | High |
| Dynamic obstacle avoidance | §2.7.1 | `kuwata_vo.py` | High |
| Scenario-Based MPC (SB-MPC) | §2.7.2 | `colav/sbmpc/sbmpc.py` | High |
| Trajectory offset provision | §2.7.2 | `sbmpc.py` | High |

### 8.2 Architecture

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| Multi-layer COLAV architecture | §2.7.3 | `colav_interface.py` - `LayerConfig` | High |
| Layer 1: Static obstacle (COLREGS-free) | §2.7.3 | `colav_interface.py` | High |
| Layer 2: Mid-level MPC-based COLAV | §2.7.3 | `colav/sbmpc/sbmpc.py` | High |
| Layer 3: Reactive VO for emergency | §2.7.3 | `colav/kuwata_vo_alg/kuwata_vo.py` | High |
| COLREGS rule compliance | §2.7.3 | Algorithm-dependent | High |
| ICOLAV interface for pluggable COLAV | §2.7 | `colav/colav_interface.py` | High |

---

## 9. Simulation Architecture

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| ERT (Explicit Runge-Kutta 4th order) integration | §2.8 | `integrators.py` - `erk4_integration_step()` | High |
| Simulation time management (t, t_start, t_end, dt) | §2.8 | `simulator.py` | High |
| Multi-ship simulation | §2.8 | `simulator.py` - `ship_list` | High |
| Ownship tracking-only mode | §2.8 | `simulator.py` - `tracking_from_ownship_only` | Medium |
| YAML scenario loading | §2.8 | `scenario_config.py`, `scenario_generator.py` | High |

---

## 10. Reinforcement Learning Integration

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| Gymnasium-compatible RL environment | §3 | `gym/environment.py` - `COLAVEnvironment` | High |
| Episode-based simulation | §3 | `gym/environment.py` | High |
| Configurable action space | §3 | `gym/action.py` | High |
| Configurable observation space | §3 | `gym/observation.py` | High |
| Customizable reward function | §3 | `gym/reward.py` | High |
| Render modes (human, rgb_array, none) | §3 | `gym/environment.py` | High |
| Video recording support | §3 | `gym/environment.py` - `metadata.video.frames_per_second` | Medium |
| Scenario file folder loading | §3 | `gym/environment.py` | High |
| Scenario shuffling | §3 | `gym/environment.py` - `shuffle_loaded_scenario_data` | Medium |
| Max episodes limit | §3 | `gym/environment.py` - `max_number_of_episodes` | Medium |
| Scenario merging | §3 | `gym/environment.py` - `merge_loaded_scenario_episodes` | Medium |
| Environment seeding | §3 | `gym/environment.py` - `seed` | Medium |
| Vectorized environment support | §3 | `gym/environment.py` | Medium |
| Logger integration | §3 | `gym/logger.py` | Medium |

---

## 11. Scenario Generation

### 11.1 Behavior Generation

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| Head-on scenario generation | §4.1 | `behavior_generator.py` | High |
| Overtaking scenario generation | §4.1 | `behavior_generator.py` | High |
| Crossing scenario generation | §4.1 | `behavior_generator.py` | High |
| Configurable bearing ranges (ho/cr/ot) | §4.1 | `scenario_generator.py` | High |
| Configurable course variation | §4.1 | `scenario_generator.py` | High |
| Distance between ships variation | §4.1 | `scenario_generator.py` - `dist_between_ships_range` | High |
| Gaussian state perturbation | §4.1 | `scenario_generator.py` - `gaussian_csog_state_perturbation_covariance` | Medium |
| Perpendicular state perturbation | §4.1 | `scenario_generator.py` - `perpendicular_csog_state_perturbation_pm_range` | Medium |
| CPA threshold filtering (t_cpa, d_cpa) | §4.1 | `scenario_generator.py` | Medium |
| Manual episode acceptance | §4.1 | `scenario_generator.py` - `manual_episode_accept` | Low |
| Random ship position generation | §4.1 | `map_functions.py` | High |
| RRT-based behavior generation | §4.1 | `rrt-rs` external library | Medium |

### 11.2 Scenario Configuration

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| YAML-based scenario specification | §4.2 | Scenario files in `scenarios/` | High |
| Waypoint-based mission planning | §4.2 | `scenario_config.py`, `ship.py` | High |
| Speed plan specification | §4.2 | `ship.py` - `speed_plan` | High |
| Ship start/end time (t_start, t_end) | §4.2 | `ship.py` | Medium |
| Goal state specification | §4.2 | `ship.py` - `goal_csog_state` | Medium |

---

## 12. Visualization

### 12.1 Live Plot

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| Real-time live plotting (matplotlib) | §5.1 | `viz/visualizer.py` | High |
| Zoom on ownship | §5.1 | `viz/visualizer.py` - `zoom_in_liveplot_on_ownship` | High |
| Configurable zoom window width | §5.1 | `viz/visualizer.py` - `zoom_window_width` | Medium |
| COLAV results visualization | §5.1 | `viz/visualizer.py` - `show_liveplot_colav_results` | High |
| Target/ownship waypoint display | §5.1 | `viz/visualizer.py` - `show_liveplot_*_waypoints` | Medium |
| Trajectory display (ownship + targets) | §5.1 | `viz/visualizer.py` - `show_liveplot_*_trajectory` | High |
| Measurement display (radar/AIS) | §5.1 | `viz/visualizer.py` - `show_liveplot_measurements` | Medium |
| Disturbance vector display | §5.1 | `viz/visualizer.py` - `show_liveplot_disturbances` | Medium |
| Scale bar | §5.1 | `viz/visualizer.py` - `show_liveplot_scalebar` | Medium |
| Simulation time overlay | §5.1 | `viz/visualizer.py` - `show_liveplot_time` | Medium |
| Dark mode | §5.1 | `viz/visualizer.py` - `dark_mode_liveplot` | Low |
| Configurable update rate | §5.1 | `viz/visualizer.py` - `update_rate_liveplot` | Medium |
| Configurable ship colors | §5.1 | `viz/visualizer.py` - `ship_colors`, `do_colors` | Low |
| Ship scaling | §5.1 | `viz/visualizer.py` - `ship_scaling` | Medium |
| Black land/shore mode | §5.1 | `viz/visualizer.py` - `black_land`, `black_shore` | Low |

### 12.2 Output Generation

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| GIF animation saving | §5.2 | `viz/visualizer.py` - `save_liveplot_animation` | High |
| PNG figure saving | §5.2 | `viz/visualizer.py` - `save_result_figures` | High |
| Trajectory tracking results plot | §5.2 | `viz/visualizer.py` - `show_trajectory_tracking_results` | Medium |
| Target tracking results plot | §5.2 | `viz/visualizer.py` - `show_target_tracking_results` | Medium |
| Scenario snapshot generation | §5.2 | `viz/visualizer.py` - `n_snapshots` | Medium |
| Pickle result export | §5.2 | `viz/visualizer.py` | Medium |
| CSV DataFrame export | §6.1 | `simulator.py` | High |

---

## 13. Data Handling

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| YAML scenario loading | §6.1 | `scenario_config.py`, `scenario_generator.py` | High |
| Simulation result saving | §6.1 | `simulator.py` - `save_scenario_results` | High |
| AIS data import from CSV | §6.2 | `vessel_data.py`, `data/ais/` | High |
| MMSI-based ship matching | §6.2 | `ship.py` | High |

---

## 14. Batch Execution

| Capability | Paper Ref | Code Location | C++ Priority |
|---|---|---|---|
| Multi-scenario batch runner | §6 | `run_all_scenarios.py` | Medium |
| Per-scenario JSON report | §6 | `scenario_run_report.json` | Low |
| GIF/PNG output per scenario | §6 | `output/animations/`, `output/figures/` | High |

---

## 15. External Library Dependencies

For C++ implementation, these require native equivalents or bindings:

| Library | Purpose | Replacement Needed |
|---|---|---|
| `seacharts` | ENC loading/visualization | OGR (GDAL) + custom renderer |
| `geopandas` / `shapely` | Geometry operations | GEOS |
| `geopy` | Geodetic transformations | PROJ |
| `vimmjipda` | Multi-target tracking (JPDA/MHT) | Custom implementation |
| `rrt-rs` | RRT-based path planning | Custom or existing C++ RRT |
| `scipy` | Numerical methods | Eigen + Boost |
| `numpy` | Array operations | Eigen::Array or std::vector |
| `matplotlib` | Visualization | Custom OpenGL/Cairo renderer |
| `moviepy` | GIF encoding | libav / ffmpeg bindings |
| `opencv-python` | Image processing | OpenCV C++ |
| `gymnasium` | RL environment | Python only (C++ needs wrapper or Python binding) |

---

## 16. Priority Summary for C++ Implementation

### High Priority (Core simulation loop)
1. ENC chart loading + grounding detection
2. Ship models (Kinematic + Viknes 3-DOF)
3. Disturbance models (wind + current)
4. LOS guidance + FLSC controller
5. AIS + Radar sensor models
6. KF tracker + God tracker
7. Kuwata VO + SB-MPC COLAV algorithms
8. ERT integrator
9. YAML scenario loading
10. GIF/PNG visualization output

### Medium Priority
11. Multi-ship simulation
12. COLREGS rule checking
13. Kalman filter tuning (P_0, q)
14. Clutter modeling
15. Scenario generation (head-on/crossing/overtaking)
16. JPDA/MHT multi-target tracking
17. RRT-based behavior generation

### Low Priority (Nice-to-have)
18. Gymnasium RL environment (requires Python interop)
19. Monte Carlo simulation
20. Dark mode / custom theming
21. Video recording (beyond GIF)
22. Scenario merging/shuffling for RL

---

## Appendix: Verification Commands

```bash
# Verify ENC data available
ls /Users/marine/Code/colav-simulator/data/enc/

# Verify AIS data available
ls /Users/marine/Code/colav-simulator/data/ais/

# Run head-on scenario and check GIF output
cd /Users/marine/Code/colav-simulator
uv run python -c "
import colav_simulator.scenario_generator as sg
import colav_simulator.simulator as sim
import colav_simulator.core.colav.colav_interface as ci
import colav_simulator.common.paths as dp
cfg = sim.Config.from_file(dp.simulator_config)
cfg.visualizer.matplotlib_backend = 'Agg'
cfg.visualizer.show_liveplot = True
cfg.visualizer.save_liveplot_animation = True
cfg.visualizer.show_results = False
simulator = sim.Simulator(config=cfg)
sg_obj = sg.ScenarioGenerator()
scenario = sg_obj.generate(config_file=dp.scenarios / 'head_on.yaml', new_load_of_map_data=True)
sbmpc = ci.SBMPCWrapper()
simulator.run([scenario], colav_systems=[(0, sbmpc)])
"
# GIF at: output/animations/head_on1_ep001.gif

# Run all scenarios
uv run python run_all_scenarios.py
```