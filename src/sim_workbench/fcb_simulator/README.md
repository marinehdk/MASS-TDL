# fcb_simulator

FCB (Fast Craft/Boat) 4-DOF MMG ship dynamics simulator for HIL / SIL testing
of the L3 Tactical Layer. This package replaces the Multimodal Fusion mock
publisher by integrating the rudder/prop commands produced by M5 through a
proper Yasukawa & Yoshimura 2015 [R7] MMG model.

## Mathematical model

4-DOF MMG (surge / sway / yaw / roll) integrated with classic 4th-order
Runge-Kutta. State vector: `[x, y, psi, u, v, r, phi, phi_dot]`.

- Hull forces: Abkowitz polynomial in non-dimensional `(v', r')`
- Rudder forces: standard MMG with effective inflow `(u_R, v_R)`
- Propeller thrust: `K_T = k_2 J^2 + k_1 J + k_0`
- Roll: linearised 1-DOF pendulum, weakly coupled

Reference: Yasukawa, H. & Yoshimura, Y., "Introduction of MMG standard method
for ship maneuvering predictions", J. Mar. Sci. Tech., 2015 [R7]. Coefficients
for the 46 m FCB tuned per Luo & Zou 2019 patrol-vessel adaptation.

## ROS2 interface

Subscribes:
- `/m5/avoidance_plan` (`l3_msgs/AvoidancePlan`) — first waypoint drives heading + speed targets
- `/m5/reactive_override_cmd` (`l3_msgs/ReactiveOverrideCmd`) — emergency overrides (validity-gated)

Publishes:
- `/fusion/own_ship_state` (`l3_external_msgs/FilteredOwnShipState`) — 50 Hz
- `/fusion/tracked_targets` (`l3_external_msgs/TrackedTargetArray`) — 2 Hz (empty default scenario)

## Configuration

All MMG coefficients live in `config/fcb_dynamics.yaml`. No vessel constants
are hardcoded in C++ source beyond struct defaults (Backseat Driver pattern).

## Build & run

```bash
colcon build --packages-select fcb_simulator
. install/setup.bash
ros2 run fcb_simulator fcb_simulator_node \
    --ros-args --params-file src/fcb_simulator/config/fcb_dynamics.yaml
```

## Tests

```bash
colcon test --packages-select fcb_simulator
colcon test-result --verbose
```

- `test_mmg_steady_turn` — 35° rudder produces yaw rate > 0.05 rad/s
- `test_rk4_energy` — straight-line stability + propulsion balance

## Parameter status

- `G_M = 1.2 m`, `T_phi = 5.0 s` → **[TBD-HAZID]** pending FCB inclining
  experiment / sea trials. Current values are representative for a 46 m
  patrol vessel and acceptable for SIL only.
- All hull / rudder / propeller derivatives: representative FCB tuning.
  Will be re-fitted against FCB sea-trial turning circle / zigzag data
  during the HAZID RUN-001 campaign.

## Code path

PATH-H (HIL/simulation tooling) — relaxed coding standards vs M5/M7 production
code. No dynamic allocation in the dynamics hot path; fixed-size state.
