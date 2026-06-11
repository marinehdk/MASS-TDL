from ais_twin.model import RoutePoint, TrackPoint, TrackSegment
from ais_twin.risk import rank_targets


def _seg(mmsi: int, lat: float, lon: float, sog: float, cog: float) -> TrackSegment:
    return TrackSegment(
        mmsi=mmsi,
        points=(
            TrackPoint(0.0, mmsi, lat, lon, sog, cog, cog),
            TrackPoint(60.0, mmsi, lat, lon + 0.01, sog, cog, cog),
        ),
    )


def test_rank_targets_prefers_crossing_near_cpa_over_far_target():
    own_route = [
        RoutePoint(lat=-2.0, lon=106.0, target_sog_kn=10.0),
        RoutePoint(lat=-2.0, lon=107.0, target_sog_kn=10.0),
    ]
    near_crossing = _seg(100, -2.01, 106.1, 12.0, 0.0)
    far_same_dir = _seg(200, -3.5, 107.5, 12.0, 90.0)

    ranked = rank_targets(own_route, [far_same_dir, near_crossing], sim_elapsed_s=0.0, top_n=20)

    assert [r.mmsi for r in ranked] == [100, 200]
    assert ranked[0].score > ranked[1].score
    assert ranked[0].rationale.startswith("risk_score=")


def test_rank_targets_limits_top_n_and_tie_breaks_by_mmsi():
    own_route = [
        RoutePoint(lat=-2.0, lon=106.0, target_sog_kn=10.0),
        RoutePoint(lat=-2.0, lon=107.0, target_sog_kn=10.0),
    ]
    segments = [
        _seg(300, -2.1, 106.1, 10.0, 90.0),
        _seg(100, -2.1, 106.1, 10.0, 90.0),
        _seg(200, -2.1, 106.1, 10.0, 90.0),
    ]

    ranked = rank_targets(own_route, segments, sim_elapsed_s=0.0, top_n=2)

    assert [r.mmsi for r in ranked] == [100, 200]


def test_rank_targets_prefers_head_on_over_same_course_at_same_range():
    own_route = [
        RoutePoint(-2.0, 106.0, 10.0),
        RoutePoint(-2.0, 107.0, 10.0),
    ]
    same_course = _seg(100, -2.0, 106.01, 10.0, 90.0)
    head_on = _seg(200, -2.0, 106.01, 10.0, 270.0)

    ranked = rank_targets(own_route, [same_course, head_on], sim_elapsed_s=0.0, top_n=2)

    assert [r.mmsi for r in ranked] == [200, 100]
    assert ranked[0].cpa_m <= ranked[1].cpa_m


def test_rank_targets_advances_ownship_with_elapsed_time():
    own_route = [
        RoutePoint(-2.0, 106.0, 60.0),
        RoutePoint(-2.0, 107.0, 60.0),
    ]
    target = _seg(100, -2.0, 107.0, 0.0, 0.0)

    ranked = rank_targets(own_route, [target], sim_elapsed_s=3600.0, top_n=1)

    assert ranked[0].distance_m < 5000.0


def test_rank_targets_does_not_prioritize_no_closure_parallel_target():
    own_route = [
        RoutePoint(-2.0, 106.0, 10.0),
        RoutePoint(-2.0, 107.0, 10.0),
    ]
    no_closure = _seg(100, -2.0, 106.01, 10.0, 90.0)
    crossing = _seg(200, -2.01, 106.01, 10.0, 0.0)

    ranked = rank_targets(own_route, [no_closure, crossing], sim_elapsed_s=0.0, top_n=2)

    assert ranked[0].mmsi == 200
    assert ranked[1].tcpa_s == float("inf")


def test_rank_targets_skips_nan_inputs_deterministically():
    import math

    own_route = [
        RoutePoint(-2.0, 106.0, 10.0),
        RoutePoint(-2.0, 107.0, 10.0),
    ]
    bad = TrackSegment(
        mmsi=999,
        points=(TrackPoint(0.0, 999, math.nan, 106.0, 10.0, 90.0, 90.0),),
    )
    good = _seg(100, -2.01, 106.1, 12.0, 0.0)

    ranked_a = rank_targets(own_route, [bad, good], sim_elapsed_s=0.0, top_n=20)
    ranked_b = rank_targets(own_route, [good, bad], sim_elapsed_s=0.0, top_n=20)

    assert [r.mmsi for r in ranked_a] == [100]
    assert [r.mmsi for r in ranked_b] == [100]
