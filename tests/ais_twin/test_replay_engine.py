from pathlib import Path

from ais_twin.model import RankedTarget, RoutePoint, TrackPoint, TrackSegment
from ais_twin.replay_engine import load_track_segments_csv, replay_payloads_at, target_payloads


def test_target_payloads_preserve_ais_source_and_metrics():
    ranked = [
        RankedTarget(
            mmsi=123456789,
            point=TrackPoint(10.0, 123456789, -2.0, 106.0, 12.0, 91.0, 92.0),
            score=3.2,
            cpa_m=450.0,
            tcpa_s=600.0,
            distance_m=1200.0,
            rationale="risk_score=3.200",
        )
    ]

    payloads = target_payloads(ranked)

    assert payloads == [
        {
            "target_id": 123456789,
            "lat": -2.0,
            "lon": 106.0,
            "sog_kn": 12.0,
            "cog_deg": 91.0,
            "heading_deg": 92.0,
            "source_sensor": "ais",
            "cpa_m": 450.0,
            "tcpa_s": 600.0,
            "rationale": "risk_score=3.200",
        }
    ]


def test_target_payloads_falls_back_to_cog_when_heading_missing():
    ranked = [
        RankedTarget(
            mmsi=1,
            point=TrackPoint(0.0, 1, -2.0, 106.0, 8.0, 33.0, None),
            score=1.0,
            cpa_m=100.0,
            tcpa_s=10.0,
            distance_m=200.0,
            rationale="risk_score=1.000",
        )
    ]

    assert target_payloads(ranked)[0]["heading_deg"] == 33.0


def test_load_track_segments_csv_sorts_by_mmsi_and_time(tmp_path):
    path: Path = tmp_path / "tracks.csv"
    path.write_text(
        "mmsi,t_s,lat,lon,sog_kn,cog_deg,heading_deg\n"
        "2,10,-2.0,106.2,10,90,\n"
        "1,10,-2.0,106.1,10,90,91\n"
        "1,0,-2.0,106.0,10,90,91\n",
        encoding="utf-8",
    )

    segments = load_track_segments_csv(path)

    assert [s.mmsi for s in segments] == [1, 2]
    assert [p.t_s for p in segments[0].points] == [0.0, 10.0]
    assert segments[1].points[0].heading_deg is None


def test_replay_payloads_at_uses_route_and_segments():
    route = [RoutePoint(-2.0, 106.0, 10.0), RoutePoint(-2.0, 107.0, 10.0)]
    segments = [
        TrackSegment(
            mmsi=123456789,
            points=(TrackPoint(0.0, 123456789, -2.0, 106.1, 12.0, 91.0, 92.0),),
        )
    ]

    payloads = replay_payloads_at(route, segments, sim_elapsed_s=0.0, top_n=1)

    assert len(payloads) == 1
    assert payloads[0]["source_sensor"] == "ais"
