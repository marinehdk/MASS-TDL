from __future__ import annotations

import hashlib
import sqlite3
from pathlib import Path

from .config import EvidenceLibraryConfig


SCHEMA_SQL = """
create table if not exists roots (
  root_id text primary key,
  label text not null,
  source text not null,
  path_glob text not null,
  enabled integer not null,
  trusted integer not null,
  allow_retention_mutation integer not null,
  follow_symlinks integer not null,
  available integer not null default 0,
  stale integer not null default 0,
  last_rescan_at text
);
create table if not exists sessions (
  evidence_id text primary key,
  session_id text not null,
  source text not null,
  suite text not null,
  root_id text not null,
  worktree_name text,
  branch text,
  session_path text not null,
  created_at text,
  ended_at text,
  status text,
  valid_data integer not null default 0,
  scenario_count integer not null default 0,
  ingest_status text not null,
  ingest_error text,
  raw_trace_policy text not null,
  latest_mtime real not null default 0
);
create table if not exists scenarios (
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  run_id text,
  verdict text,
  overall_pass integer,
  first_failure text,
  first_failed_gate text,
  first_failed_module text,
  role text,
  cpa_floor_m real,
  min_cpa_m real,
  min_cpa_nm real,
  returned_to_route integer,
  route_return_required integer,
  route_corridor_ok integer,
  stability_pass integer,
  compliance_verdict text,
  primary key (evidence_id, scenario_id)
);
create table if not exists artifacts (
  artifact_id integer primary key autoincrement,
  evidence_id text not null,
  session_id text not null,
  scenario_id text,
  kind text not null,
  path text not null,
  relative_path text not null,
  sha256 text,
  mtime real,
  compressed integer not null default 0,
  available integer not null default 1
);
create table if not exists trajectory_samples (
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  vessel_id text not null,
  vessel_role text not null,
  sim_t real not null,
  wall_t real,
  lat real,
  lon real,
  heading_deg real,
  sog_kn real,
  rot_deg_s real,
  source_topic text,
  sample_seq integer not null,
  primary key (evidence_id, scenario_id, vessel_id, sim_t, sample_seq)
);
create index if not exists idx_trajectory_window on trajectory_samples(evidence_id, scenario_id, sim_t);
create index if not exists idx_trajectory_vessel on trajectory_samples(evidence_id, scenario_id, vessel_id, sim_t);
create table if not exists trajectory_downsample (
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  vessel_id text not null,
  vessel_role text not null,
  sim_t real not null,
  wall_t real,
  lat real,
  lon real,
  heading_deg real,
  sog_kn real,
  rot_deg_s real,
  source_topic text,
  sample_seq integer not null,
  level integer not null,
  primary key (evidence_id, scenario_id, vessel_id, level, sim_t, sample_seq)
);
create table if not exists state_segments (
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  module text not null,
  field text not null,
  start_t real not null,
  end_t real not null,
  value_json text not null,
  source_topic text
);
create table if not exists events (
  event_id integer primary key autoincrement,
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  sim_t real not null,
  wall_t real,
  module text not null,
  event_type text not null,
  severity text not null,
  payload_json text not null,
  source_topic text
);
create index if not exists idx_events_time on events(evidence_id, scenario_id, sim_t);
create table if not exists gate_results (
  evidence_id text not null,
  session_id text not null,
  scenario_id text not null,
  gate_id text not null,
  status text not null,
  temporal_scope text not null,
  precedence_rank integer not null,
  conflict_group text,
  payload_json text not null,
  source text not null
);
"""


def compute_evidence_id(root_id: str, session_path: Path) -> str:
    canonical = session_path.resolve()
    return hashlib.sha256(f"{root_id}:{canonical}".encode("utf-8")).hexdigest()


def open_database(config: EvidenceLibraryConfig) -> sqlite3.Connection:
    config.database_path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(config.database_path)
    conn.row_factory = sqlite3.Row
    conn.execute("pragma foreign_keys = on")
    return conn


def initialize_schema(conn: sqlite3.Connection) -> None:
    conn.executescript(SCHEMA_SQL)
    conn.commit()
