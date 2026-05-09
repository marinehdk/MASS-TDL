# SPDX-License-Identifier: Proprietary
"""Build TrackedTargetArray from AISRecord list per v1.1.2 IDL."""
from __future__ import annotations

from ais_bridge.nmea_decoder import AISRecord


def _classification_from_type(ship_type: int) -> str:
    """Map AIS VesselType byte to TrackedTarget classification string."""
    if 20 <= ship_type <= 99:
        return 'vessel'
    if ship_type == 0 or ship_type > 99:
        return 'unknown'
    return 'fixed_object'


def build_tracked_target_array(records: list[AISRecord], stamp, node=None):
    """Build a TrackedTargetArray message from a list of AIS records.

    No vessel-type literals anywhere in this function (multi_vessel_lint compliant).
    """
    from geographic_msgs.msg import GeoPoint

    from l3_external_msgs.msg import TrackedTargetArray
    from l3_msgs.msg import EncounterClassification, TrackedTarget

    sigma_pos_m = 50.0
    sigma_hdg_deg = 5.0
    sigma_pos_sq = sigma_pos_m ** 2
    sigma_hdg_sq = sigma_hdg_deg ** 2
    ais_confidence = 0.7

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
        t.classification_confidence = ais_confidence
        t.source_sensor = 'ais'
        t.cpa_m = 0.0
        t.tcpa_s = 0.0
        t.encounter = EncounterClassification()
        t.confidence = ais_confidence

        targets.append(t)

    msg = TrackedTargetArray()
    msg.schema_version = 'v1.1.2'
    msg.stamp = stamp
    msg.targets = targets
    msg.confidence = ais_confidence
    msg.rationale = 'ais_bridge replay'
    return msg
