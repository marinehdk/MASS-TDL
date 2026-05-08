# SPDX-License-Identifier: Proprietary
"""Build TrackedTargetArray from AISRecord list per v1.1.2 IDL."""
from __future__ import annotations

from typing import List
from ais_bridge.nmea_decoder import AISRecord


def _classification_from_type(ship_type: int) -> str:
    """Map AIS VesselType byte to TrackedTarget classification string."""
    if 20 <= ship_type <= 99:
        return 'vessel'
    if ship_type == 0 or ship_type > 99:
        return 'unknown'
    return 'fixed_object'


def build_tracked_target_array(records: List[AISRecord], stamp, node=None):
    """Build a TrackedTargetArray message from a list of AIS records.

    No vessel-type literals anywhere in this function (multi_vessel_lint compliant).
    """
    from l3_external_msgs.msg import TrackedTargetArray
    from l3_msgs.msg import TrackedTarget, EncounterClassification
    from geographic_msgs.msg import GeoPoint

    SIGMA_POS_M = 50.0
    SIGMA_HDG_DEG = 5.0
    sigma_pos_sq = SIGMA_POS_M ** 2
    sigma_hdg_sq = SIGMA_HDG_DEG ** 2
    AIS_CONFIDENCE = 0.7

    targets = []
    for rec in records:
        t = TrackedTarget()
        t.schema_version = 'v1.1.2'
        t.stamp = stamp
        t.target_id = rec.mmsi

        pos = GeoPoint()
        pos.latitude = rec.lat
        pos.longitude = rec.lon
        pos.altitude = 0.0
        t.position = pos

        t.sog_kn = rec.sog_kn
        t.cog_deg = rec.cog_deg
        t.heading_deg = rec.heading_deg

        t.covariance = [
            sigma_pos_sq, 0.0, 0.0,
            0.0, sigma_pos_sq, 0.0,
            0.0, 0.0, sigma_hdg_sq,
        ]

        t.classification = _classification_from_type(rec.ship_type)
        t.classification_confidence = AIS_CONFIDENCE
        t.source_sensor = 'ais'
        t.cpa_m = 0.0
        t.tcpa_s = 0.0
        t.encounter = EncounterClassification()
        t.confidence = AIS_CONFIDENCE

        targets.append(t)

    msg = TrackedTargetArray()
    msg.schema_version = 'v1.1.2'
    msg.stamp = stamp
    msg.targets = targets
    msg.confidence = AIS_CONFIDENCE
    msg.rationale = 'ais_bridge replay'
    return msg
