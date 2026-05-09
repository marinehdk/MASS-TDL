#!/usr/bin/env python3
"""Smoke test for fcb_sim_py pybind11 binding (D1.3b T6 acceptance criteria)."""
import math
import sys


def main():
    try:
        import fcb_sim_py
    except ImportError as e:
        print(f"FAIL: cannot import fcb_sim_py: {e}")
        print("Ensure colcon build succeeded and 'source install/setup.bash' was run.")
        sys.exit(1)

    # Test 1: FcbState construction and field access
    s = fcb_sim_py.FcbState()
    assert s.x == 0.0, f"Expected x=0.0, got {s.x}"
    assert s.u == 0.0, f"Expected u=0.0, got {s.u}"

    # Test 2: Field assignment
    s.u = 6.17    # 12 kn
    s.psi = math.pi / 2.0  # heading north (math convention)
    assert abs(s.u - 6.17) < 1e-9
    assert abs(s.psi - math.pi / 2.0) < 1e-9

    # Test 3: MmgParams construction
    p = fcb_sim_py.MmgParams()

    # Test 4: rk4_step — surge must remain positive, heading must be stable
    s2 = fcb_sim_py.rk4_step(s, 0.0, 3.5, p, 0.02)
    assert s2.u > 0.0, f"Surge dropped non-positive: {s2.u}"
    assert math.isfinite(s2.u), f"Surge is not finite: {s2.u}"
    assert math.isfinite(s2.psi), f"Heading is not finite: {s2.psi}"
    assert abs(s2.psi - math.pi / 2.0) < 0.05, f"Heading drifted too much: {s2.psi}"

    # Test 5: 100 steps — stability check
    state = fcb_sim_py.FcbState()
    state.u = 6.17
    state.psi = math.pi / 2.0
    params = fcb_sim_py.MmgParams()
    for _ in range(100):
        state = fcb_sim_py.rk4_step(state, 0.0, 3.5, params, 0.02)
    assert math.isfinite(state.u), "u became non-finite after 100 steps"
    assert math.isfinite(state.psi), "psi became non-finite after 100 steps"

    print("OK: all smoke tests passed")
    print(f"  u after 100 steps = {state.u:.4f} m/s")
    print(f"  psi after 100 steps = {math.degrees(state.psi):.2f} deg (math conv)")


if __name__ == "__main__":
    main()
