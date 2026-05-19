# Salvage: feat/d3.1-m4-behavior-arbiter (verbatim snapshot)

Imported from deleted branch `feat/d3.1-m4-behavior-arbiter` (16 commits, ~3180 LOC, Tasks 1-6).

**Status: REFERENCE ONLY. NOT BUILT.** This directory is excluded from colcon by virtue
of the leading dot in the name. Main's `m4_behavior_arbiter/` package at the parent
level is the active implementation.

## Why salvaged

Branch built against pre-2026-05-13 design baseline:
- Wrong package path (`src/m4_behavior_arbiter/` instead of `src/l3_tdl_kernel/m4_*`).
- Custom `asdr_logger.hpp` predates `l3_msgs/ASDRRecord.msg` IDL.
- M4 detailed design ARCHIVED to `docs/Design/Phase 1/Archive/Old Modules/M4-Behavior-Arbiter/`
  pending Phase 2/3 re-spec.

But branch IvP impl (1810 LOC src + 1170 LOC tests across 8 unit-test files) carries
genuine engineering value. Preserved here so the future D3.1 re-spec can cross-reference
this implementation against main's parallel `m4_*` skeleton.

## TODOs before any of this code can be activated

1. TODO(D3.1-rehome): Move files up one level, merging with main's `m4_behavior_arbiter/`.
2. TODO(D3.1-rehome): Replace `asdr_logger.hpp/cpp` includes with `l3_msgs/ASDRRecord` publisher.
3. TODO(D3.1-rehome): Reconcile `behavior_arbiter.hpp/cpp` (branch) vs `behavior_arbiter_node.hpp/cpp` (main).
4. TODO(D3.1-rehome): Re-spec M4 in Phase 2/3 before merging implementations.

See `/tmp/branches-review.md` (review report) for details.
