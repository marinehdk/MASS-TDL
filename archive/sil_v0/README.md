# SIL v0 Prototype Archive

Archived 2026-05-12 per greenfield decision.
See `docs/Design/SIL/2026-05-12-sil-architecture-design.md` §9.1.

Retired modules replaced by the new SIL architecture:
- `web/` — 13 React components + 3 hooks (replaced by 4-screen Zustand + RTK Query app)
- `sil_mock_publisher/` — ROS2 mock publisher (replaced by 9-node rclcpp_components)
- `l3_external_mock_publisher/` — L3 mock publisher (retired)

Keep until DEMO-1 passes (2026-06-15), then delete.
