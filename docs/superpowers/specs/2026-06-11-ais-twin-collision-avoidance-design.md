# AIS Twin Collision Avoidance Design

## Goal

Build a reproducible AIS digital-twin collision-avoidance scenario for the existing safe-route integration area. Static map mode displays real AIS targets from a live source. Simulation mode replays a captured AIS dataset so the full TDL avoidance chain can be reproduced.

## Scope

- Route source: `/l2/planned_route`, with `scenarios/集成测试/safe_route.yaml` as the MVP route fixture.
- AIS region: safe-route bounding box plus 20 nautical miles.
- AIS capture bounding box: latitude `-4.503333` to `-1.136667`, longitude `104.786263` to `108.513737`.
- Route duration: about `9.56 h`.
- AIS capture duration: `10 h`.
- AIS provider: AISstream first, with provider abstraction for later replacement.
- TDL input: Top-20 risk-ranked AIS targets published as `/fusion/tracked_targets`.
- Acceptance: complete the full route, display AIS targets during the run, trigger at least three avoidance events, and produce ASDR evidence.

## Architecture

```text
L2 Route Source
  -> /l2/planned_route

AISProvider(AISstream)
  -> raw AIS stream

DatasetStore
  -> raw JSONL + normalized tracks + manifest

RiskSelector
  -> Top-20 risk-ranked targets

TwinReplayPublisher
  -> /fusion/tracked_targets

TDL M2/M4/M5/M6/M7
  -> /l3/asdr/record

HMI
  -> static live AIS + replay AIS layer
```

## Components

### AISProvider

The provider owns the live source connection. The MVP provider is AISstream over WebSocket. The provider accepts a bounding box and emits canonical AIS records. API keys remain in environment variables or local config and are never written to dataset artifacts.

### DatasetStore

The store writes append-only raw JSONL, normalized CSV tracks, and a manifest. The manifest records provider name, capture times, bbox, input route, generated file hashes, quality counters, and time alignment.

### TrackNormalizer

The normalizer groups records by MMSI, sorts by AIS time, removes exact duplicate timestamps, splits long gaps, validates lat/lon bounds, and carries quality flags for missing SOG, COG, heading, or AIS timestamp.

### RiskSelector

The selector evaluates target tracks against ownship state on the planned route. It ranks targets by a weighted score using CPA, TCPA, current distance, SOG, and crossing angle, then publishes only the Top-20 targets to TDL.

### TwinReplayPublisher

The replay publisher reads only frozen dataset files. It maps simulation elapsed time to original AIS capture time without time warping:

```text
T_route = 9.56h
T_ais_capture = 10h
ais_time = capture_start + sim_elapsed
replay_end = capture_start + T_route
published_sog = original_sog
manifest.time_alignment = "real_time_trim"
```

If a future run uses shorter captures, it must set `manifest.time_alignment = "time_warp"` and mark acceptance as degraded.

### StaticAISMap

Static map mode reads from a backend cache/API, not directly from the AIS provider. The first implementation exposes an isolated debug map. After that passes, the existing HMI map gets a dedicated AIS target layer.

## Canonical AIS Record

```text
CanonicalAISRecord:
provider
received_at_utc
ais_time_utc
mmsi
lat
lon
sog_kn
cog_deg
heading_deg
nav_status
ship_name?
ship_type?
raw_message_type
raw_json
quality_flags
```

## Interfaces

- Route input: `/l2/planned_route`.
- Target output: `/fusion/tracked_targets`, message type `l3_external_msgs/TrackedTargetArray`.
- Decision record output: existing `/l3/asdr/record`, bridged to `/sil/asdr_event`.
- Static AIS API: backend latest-target cache for debug map and HMI layer.

## Data Quality Gates

- A 10-hour capture must produce `raw.jsonl`, `tracks.csv`, and `manifest.yaml`.
- At least one valid AIS position report must be captured.
- Each replayable track segment must have at least three valid points.
- All replayed positions must lie inside the configured bbox.
- Missing SOG, COG, heading, or AIS timestamp must set quality flags.
- If the captured dataset cannot produce at least three avoidance events, the run fails as dataset-insufficient. The system must not fabricate targets or ASDR decisions.

## Error Handling

- WebSocket disconnects use exponential-backoff reconnect.
- Provider no-data windows are recorded as health events in the manifest.
- Provider throttling reduces display/update rate and does not widen bbox automatically.
- Hash mismatch rejects replay.
- Replay mode must prevent competing `/fusion/tracked_targets` publishers from mixing SIL mock targets and AIS Twin targets in the same acceptance run.

## Acceptance

1. AISstream capture runs for 10 hours over the configured bbox and writes raw, normalized, manifest, and hash artifacts.
2. Static debug map displays live or near-live AIS targets from the backend cache.
3. Replay uses the frozen dataset and covers the full safe route duration.
4. TDL receives Top-20 AIS targets through `/fusion/tracked_targets`.
5. Full-route simulation triggers at least three avoidance events.
6. ASDR includes threat identification, rule/behavior decision, avoidance plan, and recovery/end evidence.
7. Repeating the same replay with the same dataset, route, and config produces the same event sequence.

## Current Code Anchors

- Existing route ingest publishes `/l2/planned_route`: `docker/route_ingest_node.py`.
- Existing bridge documents `/fusion/tracked_targets` as the TDL target input: `docker/sil_topic_bridge.py`.
- Existing bridge forwards ASDR records to SIL: `docker/sil_topic_bridge.py`.
- Existing `scenario_authoring` AIS replay is not the primary implementation target because it publishes `/world_model/tracks` and does not replay full historical target trajectories.

## Source Confidence

- Medium: AISstream documents WebSocket API-key bbox subscriptions in official documentation, but service stability and regional coverage must be tested for the safe-route bbox.
- Low: Public reports indicate possible AISstream no-data or connection issues in some conditions. The design therefore freezes datasets and keeps providers replaceable.
