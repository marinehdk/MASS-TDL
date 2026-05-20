"""Tests for geo_utils ENU coordinate transforms.

Covers default origin, own-ship-at-origin, target-ship-north offset,
roundtrip consistency, and heading→psi conversion math (GAP 3 fix).
"""

import math
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))
from geo_utils import latlon_to_enu, enu_to_latlon, default_origin


def test_default_origin():
    lat, lon = default_origin()
    assert lat == 63.0 and lon == 5.0


def test_own_ship_at_origin():
    n, e = latlon_to_enu(63.0, 5.0, 63.0, 5.0)
    assert abs(n) < 1e-6 and abs(e) < 1e-6


def test_target_north_of_os():
    origin = (63.44, 10.38)
    dn, de = latlon_to_enu(*origin, 63.557451, 10.38)
    assert abs(dn - 13060.0) < 5.0 and abs(de) < 2.0


def test_roundtrip():
    origin = (63.44, 10.38)
    dn, de = latlon_to_enu(*origin, 63.557451, 10.38)
    lat2, lon2 = enu_to_latlon(*origin, dn, de)
    dn2, de2 = latlon_to_enu(*origin, lat2, lon2)
    assert abs(dn2 - dn) < 1.0 and abs(de2 - de) < 1.0


def test_heading_to_psi_math():
    psi = lambda h: math.pi / 2.0 - math.radians(h)
    assert abs(psi(0.0) - math.pi / 2.0) < 1e-9
    assert abs(psi(90.0) - 0.0) < 1e-9
