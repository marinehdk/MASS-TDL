# Third-Party GNC Workspace — Provenance

**Snapshot source:** `/Users/marine/Code/_a4000_snapshots/mpc_latest/mpc/船舶动力学/gnc_ws`
**Original A4000 path:** `/home/mass/mpc/船舶动力学/gnc_ws`
**A4000 account:** `mass` (read-only copy to local snapshot)
**Copy date:** 2026-06-25
**ROS distro:** humble (verified: package format 3, `ros:humble-ros-base` compatible)

**Vendored subset:** `src/` only (16 packages). Build artifacts (`build_*/`, `install_dual16/`,
`log_dual16*`, `ship_feedback_logs/`, `codex_backups/`, `logs/`) from the snapshot were NOT copied.

Package layout under `third_party/gnc_ws/src/`:
- `gnc/` — `sensor_fusion`, `ship_guidance` (incl. `active_route_manager_node`), `ship_control`, `thrust_allocation`
- `platform/` — `ship_bringup` (launch), `ship_interfaces` (13 `.msg`), `ship_description`, `ship_utils`
- `simulation/` — `sim_clock`, `ship_dynamics` (the real plant), `env_engines`, `mock_scenarios`
- `route_planning_cpp/`, `route_planning_ros2/`
- `mission/mission_supervisor/`, `safety/safety_supervisor/`

## Re-sync procedure (when colleague updates GNC)

1. Re-copy the updated `src/` from the updated A4000 path or local snapshot into `third_party/gnc_ws/src/`.
   Replace wholesale — do NOT merge.
2. Update the snapshot date above.
3. Rebuild the GNC image: `docker compose -f docker-compose.gnc.yml build`.
4. Run the Track A acceptance (Task A7) to verify the bridge still maps fields correctly.
5. If the colleague changed `ship_interfaces` field names, update **only** `src/sim_workbench/gnc_bridge/src/translators.{hpp,cpp}`.

## Do NOT edit GNC source

The GNC stack runs as-is. Parameter tuning is via `docker/gnc-ship-config-overlay.yaml` mount overlay,
not source edits. See Track A spec §2.2 (Out of Scope) and design decision D2.
