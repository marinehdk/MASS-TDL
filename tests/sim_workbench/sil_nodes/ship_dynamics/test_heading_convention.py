import math
import os
import sys

import pytest

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(__file__),
        "../../../../src/sim_workbench/sil_nodes/ship_dynamics",
    ),
)

from ship_dynamics.node import _ground_track_to_nav_cog, _math_heading_to_nav_heading


def test_math_heading_is_published_as_nautical_heading():
    assert _math_heading_to_nav_heading(math.pi / 2.0) == pytest.approx(0.0)
    assert _math_heading_to_nav_heading(0.0) == pytest.approx(math.pi / 2.0)
    assert _math_heading_to_nav_heading(math.pi) == pytest.approx(3.0 * math.pi / 2.0)


def test_ground_track_is_published_as_nautical_cog():
    assert _ground_track_to_nav_cog(math.pi / 2.0, 5.0, 0.0) == pytest.approx(0.0)
    assert _ground_track_to_nav_cog(0.0, 5.0, 0.0) == pytest.approx(math.pi / 2.0)
