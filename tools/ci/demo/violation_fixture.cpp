// SPDX-License-Identifier: Proprietary
// DEMO-1 6/15 fixture — DELIBERATE RL-ISOLATION VIOLATION for live demo only.
// Per spec §v3.1.10 Demo Charter. Path tools/ci/demo/ is NOT scanned by lint;
// at demo time, this is `cp`'d into src/sim_workbench/fcb_simulator/src/ as
// _demo_violation.cpp (lint scans + fails), then `rm`'d. Main branch source
// tree NEVER contains the copy.
#include "m7_safety_supervisor/safety_supervisor.hpp"

namespace demo {
inline void unused() { /* no-op fixture */ }
}
