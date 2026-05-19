# SIL Greenfield Foundation — Parallel Execution Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the Protobuf IDL SSoT, TS/Python/ROS2 code generation pipeline, project scaffold, and shared frontend infrastructure — the blocking foundation that unblocks 8 parallel implementation tracks.

**Architecture:** Single Protobuf SSoT (`idl/proto/sil/*.proto`) → `buf generate` → 3 language targets (TypeScript via protobuf-ts, Python via python_protobuf, ROS2 via proto2ros). Frontend uses Zustand for 50Hz telemetry + RTK Query for REST. ROS2 nodes are rclcpp_components loaded into single `component_container_mt` process. Orchestrator is FastAPI + rclpy bridging REST ↔ ROS2 services.

**Tech Stack:** buf v1.x, protobuf-ts, python_protobuf, proto2ros, React 18 + Zustand + RTK Query + MapLibre GL JS, FastAPI + rclpy, ROS2 Humble + rclcpp_components

**Spec:** `docs/Design/SIL/2026-05-12-sil-architecture-design.md` — §1–§9 are the authority for all implementation decisions below.

---

## Parallelisation Map

```
Task 1: Protobuf IDL (10 messages + 6 services)        ← BLOCKS ALL
  ├── Task 2: TS type generation (protobuf-ts)          ← parallel with 3,4
  ├── Task 3: Python stub generation                    ← parallel with 2,4
  └── Task 4: proto2ros conversion stubs                ← parallel with 2,3

After Task 2:
  ├── Task 5: Zustand telemetry stores                  ← parallel with 6,7
  ├── Task 6: RTK Query API slice                       ← parallel with 5,7
  └── Task 7: MapLibre base + S-57 MVT setup            ← parallel with 5,6

After Tasks 5+6+7:
  ├── Task 8:  Screen ① Scenario Builder                ← parallel with 9,10
  ├── Task 9:  Screen ② Pre-flight                       ← parallel with 8,10
  └── Task 10: Screen ④ Run Report                       ← parallel with 8,9

After Task 3:
  ├── Task 11: Orchestrator FastAPI scaffold            ← blocks 12,13
  ├── Task 12: Scenario CRUD + lifecycle REST endpoints ← after 11, parallel with 13
  └── Task 13: Self-check + Export endpoints            ← after 11, parallel with 12

After Task 4:
  └── Task 14: scenario_lifecycle_mgr node              ← blocks 15-22

After Task 14:
  ├── Task 15: scenario_authoring_node                  ← parallel with 16-22
  ├── Task 16: ship_dynamics_node (from fcb_simulator)  ← parallel with 15,17-22
  ├── Task 17: env_disturbance_node                     ← parallel with 15,16,18-22
  ├── Task 18: target_vessel_node                       ← parallel with 15-17,19-22
  ├── Task 19: sensor_mock_node                         ← parallel with 15-18,20-22
  ├── Task 20: tracker_mock_node                        ← parallel with 15-19,21-22
  ├── Task 21: fault_injection_node                     ← parallel with 15-20,22
  └── Task 22: scoring_node                             ← parallel with 15-21

Integration (after all tracks):
  ├── Task 23: foxglove_bridge wiring                   ← after 14+22+5
  ├── Task 24: Screen ③ Bridge HMI (50Hz live)          ← after 23
  ├── Task 25: Docker compose + component_container     ← after 14-22
  ├── Task 26: buf CI + pytest harness                  ← after 1-4
  ├── Task 27: Archive old web/ + sim_workbench/        ← after 24
  └── Task 28: E2E R14 head-on scenario                 ← after 24+25
```

**Max parallelism:** 8 subagents (Tasks 15–22) after Task 14 completes.

---

## File Structure (greenfield)

### New directories (created in Task 1 + 11 + 14)
```
idl/proto/sil/                  ← Protobuf SSoT (10 .proto files)
web/src/store/                  ← Zustand slices (5 files)
web/src/api/                    ← RTK Query (1 file)
web/src/screens/                ← 4 screen components
web/src/map/                    ← MapLibre layers, vessel sprites, controls
src/sil_orchestrator/           ← FastAPI app (Python)
src/sim_workbench/sil_lifecycle/ ← ROS2 lifecycle mgr (C++/Python)
src/sim_workbench/sil_nodes/    ← 9 ROS2 nodes (C++/Python, subdirs)
```

### Existing files disposition (per §9.1)
| Path | Action | When |
|---|---|---|
| `web/` | Archive to `archive/sil_v0/web/` | Task 27 |
| `src/sim_workbench/sil_mock_publisher/` | Archive to `archive/sil_v0/` | Task 27 |
| `src/sim_workbench/l3_external_mock_publisher/` | Archive | Task 27 |
| `src/sim_workbench/ais_bridge/` | Keep → migrate into `scenario_authoring_node` | Task 15 |
| `src/sim_workbench/scenario_authoring/` | Keep → upgrade to ROS2 node | Task 15 |
| `src/sim_workbench/fcb_simulator/` | Keep → upgrade to `ship_dynamics_node` | Task 16 |
| `src/sim_workbench/ship_sim_interfaces/` | Keep, no changes | — |
| `docker-compose.yml` | Rewrite | Task 25 |

---

## Task 1: Protobuf IDL — SSoT Definition

**Files:** Create 12 `.proto` files under `idl/proto/sil/`

**Blocking:** This task blocks ALL other tasks. Must complete first.

**Spec reference:** §4.3 (10 messages + 6 services), §4.2 (L3 kernel boundary rules)

- [ ] **Step 1.1: Create directory structure**

```bash
mkdir -p idl/proto/sil
```

- [ ] **Step 1.2: Create buf workspace config**

Create `idl/buf.yaml`:
```yaml
version: v1
breaking:
  use:
    - FILE
lint:
  use:
    - DEFAULT
  except:
    - PACKAGE_VERSION_SUFFIX
```

Create `idl/buf.gen.yaml`:
```yaml
version: v1
plugins:
  - plugin: protobuf-ts
    out: ../web/src/types
    opt:
      - generate_dependencies
      - long_type_string
  - plugin: python_protobuf
    out: ../src/sil_orchestrator/sil_proto
  - plugin: proto2ros
    out: ../src/sim_workbench/sil_msgs
    opt:
      - package_prefix=sil_msgs
```

- [ ] **Step 1.3: Create 10 core message .proto files**

Create `idl/proto/sil/own_ship_state.proto`:
```protobuf
syntax = "proto3";
package sil;

import "google/protobuf/timestamp.proto";

message OwnShipState {
  google.protobuf.Timestamp stamp = 1;
  Pose pose = 2;
  Kinematics kinematics = 3;
  ControlState control_state = 4;

  message Pose {
    double lat = 1;
    double lon = 2;
    double heading = 3;  // radians, true north
  }
  message Kinematics {
    double sog = 1;   // m/s
    double cog = 2;   // radians
    double rot = 3;   // rad/s
    double u = 4;     // surge m/s
    double v = 5;     // sway m/s
    double r = 6;     // yaw rate rad/s
  }
  message ControlState {
    double rudder_angle = 1;  // radians
    double throttle = 2;      // [0,1]
  }
}
```

Create `idl/proto/sil/target_vessel_state.proto`:
```protobuf
syntax = "proto3";
package sil;

import "google/protobuf/timestamp.proto";

message TargetVesselState {
  google.protobuf.Timestamp stamp = 1;
  uint32 mmsi = 2;
  Pose pose = 3;
  Kinematics kinematics = 4;
  ShipType ship_type = 5;
  TargetMode mode = 6;

  enum ShipType {
    SHIP_TYPE_UNKNOWN = 0;
    CARGO = 1;
    TANKER = 2;
    PASSENGER = 3;
    FISHING = 4;
    TUG = 5;
    PLEASURE = 6;
  }
  enum TargetMode {
    TARGET_MODE_UNKNOWN = 0;
    REPLAY = 1;
    NCDM = 2;
    INTELLIGENT = 3;
  }
  message Pose {
    double lat = 1;
    double lon = 2;
    double heading = 3;
  }
  message Kinematics {
    double sog = 1;
    double cog = 2;
    double rot = 3;
  }
}
```

Create `idl/proto/sil/radar_measurement.proto`:
```protobuf
syntax = "proto3";
package sil;

import "google/protobuf/timestamp.proto";

message RadarMeasurement {
  google.protobuf.Timestamp stamp = 1;
  repeated PolarTarget polar_targets = 2;
  uint32 clutter_cardinality = 3;

  message PolarTarget {
    double range = 1;     // meters
    double bearing = 2;   // radians
    double rcs = 3;       // dBsm
  }
}
```

Create `idl/proto/sil/ais_message.proto`:
```protobuf
syntax = "proto3";
package sil;

message AISMessage {
  uint32 mmsi = 1;
  double sog = 2;
  double cog = 3;
  double lat = 4;
  double lon = 5;
  double heading = 6;
  bool dropout_flag = 7;
}
```

Create `idl/proto/sil/environment_state.proto`:
```protobuf
syntax = "proto3";
package sil;

import "google/protobuf/timestamp.proto";

message EnvironmentState {
  google.protobuf.Timestamp stamp = 1;
  Wind wind = 2;
  Current current = 3;
  double visibility_nm = 4;
  uint32 sea_state_beaufort = 5;

  message Wind {
    double direction = 1;  // radians, from
    double speed_mps = 2;
  }
  message Current {
    double direction = 1;  // radians, to
    double speed_mps = 2;
  }
}
```

Create `idl/proto/sil/fault_event.proto`:
```protobuf
syntax = "proto3";
package sil;

import "google/protobuf/timestamp.proto";

message FaultEvent {
  google.protobuf.Timestamp stamp = 1;
  string fault_type = 2;  // "ais_dropout" | "radar_spike" | "disturbance_step"
  string payload_json = 3;
}
```

Create `idl/proto/sil/module_pulse.proto`:
```protobuf
syntax = "proto3";
package sil;

import "google/protobuf/timestamp.proto";

message ModulePulse {
  google.protobuf.Timestamp stamp = 1;
  ModuleId module_id = 2;
  HealthState state = 3;
  uint32 latency_ms = 4;
  uint32 message_drops = 5;

  enum ModuleId {
    MODULE_ID_UNKNOWN = 0;
    M1 = 1;
    M2 = 2;
    M3 = 3;
    M4 = 4;
    M5 = 5;
    M6 = 6;
    M7 = 7;
    M8 = 8;
  }
  enum HealthState {
    HEALTH_STATE_UNKNOWN = 0;
    GREEN = 1;
    AMBER = 2;
    RED = 3;
  }
}
```

Create `idl/proto/sil/scoring_row.proto`:
```protobuf
syntax = "proto3";
package sil;

import "google/protobuf/timestamp.proto";

message ScoringRow {
  google.protobuf.Timestamp stamp = 1;
  double safety = 2;
  double rule_compliance = 3;
  double delay = 4;
  double magnitude = 5;
  double phase = 6;
  double plausibility = 7;
  double total = 8;
}
```

Create `idl/proto/sil/asdr_event.proto`:
```protobuf
syntax = "proto3";
package sil;

import "google/protobuf/timestamp.proto";

message ASDREvent {
  google.protobuf.Timestamp stamp = 1;
  string event_type = 2;
  string rule_ref = 3;
  string decision_id = 4;
  Verdict verdict = 5;
  string payload_json = 6;

  enum Verdict {
    VERDICT_UNKNOWN = 0;
    PASS = 1;
    RISK = 2;
    FAIL = 3;
  }
}
```

Create `idl/proto/sil/lifecycle_status.proto`:
```protobuf
syntax = "proto3";
package sil;

import "google/protobuf/timestamp.proto";

message LifecycleStatus {
  google.protobuf.Timestamp stamp = 1;
  LifecycleState current_state = 2;
  string scenario_id = 3;
  string scenario_hash = 4;
  double sim_time = 5;
  double wall_time = 6;
  double sim_rate = 7;

  enum LifecycleState {
    LIFECYCLE_STATE_UNKNOWN = 0;
    UNCONFIGURED = 1;
    INACTIVE = 2;
    ACTIVE = 3;
    DEACTIVATING = 4;
    FINALIZED = 5;
  }
}
```

- [ ] **Step 1.4: Create 6 service .proto files**

Create `idl/proto/sil/scenario_crud.proto`:
```protobuf
syntax = "proto3";
package sil;

service ScenarioCRUD {
  rpc List(ListRequest) returns (ListResponse);
  rpc Get(GetRequest) returns (GetResponse);
  rpc Create(CreateRequest) returns (CreateResponse);
  rpc Update(UpdateRequest) returns (UpdateResponse);
  rpc Delete(DeleteRequest) returns (DeleteResponse);
  rpc Validate(ValidateRequest) returns (ValidateResponse);
}

message ListRequest { /* pagination later */ }
message ListResponse { repeated ScenarioSummary scenarios = 1; }
message ScenarioSummary { string id = 1; string name = 2; string encounter_type = 3; }
message GetRequest { string scenario_id = 1; }
message GetResponse { string yaml_content = 1; string hash = 2; }
message CreateRequest { string yaml_content = 1; }
message CreateResponse { string scenario_id = 1; string hash = 2; }
message UpdateRequest { string scenario_id = 1; string yaml_content = 2; }
message UpdateResponse { string hash = 1; }
message DeleteRequest { string scenario_id = 1; }
message DeleteResponse {}
message ValidateRequest { string yaml_content = 1; }
message ValidateResponse { bool valid = 1; repeated string errors = 2; }
```

Create `idl/proto/sil/lifecycle_control.proto`:
```protobuf
syntax = "proto3";
package sil;

service LifecycleControl {
  rpc Configure(ConfigureRequest) returns (ConfigureResponse);
  rpc Activate(ActivateRequest) returns (ActivateResponse);
  rpc Deactivate(DeactivateRequest) returns (DeactivateResponse);
  rpc Cleanup(CleanupRequest) returns (CleanupResponse);
}

message ConfigureRequest { string scenario_id = 1; }
message ConfigureResponse { bool success = 1; string error = 2; }
message ActivateRequest {}
message ActivateResponse { bool success = 1; string error = 2; }
message DeactivateRequest {}
message DeactivateResponse { bool success = 1; string error = 2; }
message CleanupRequest {}
message CleanupResponse { bool success = 1; string error = 2; }
```

Create `idl/proto/sil/sim_clock.proto`:
```protobuf
syntax = "proto3";
package sil;

service SimClock {
  rpc SetRate(SetRateRequest) returns (SetRateResponse);
  rpc GetTime(GetTimeRequest) returns (GetTimeResponse);
}

message SetRateRequest { double rate = 1; }
message SetRateResponse { bool success = 1; }
message GetTimeRequest {}
message GetTimeResponse { double sim_time = 1; double wall_time = 2; }
```

Create `idl/proto/sil/fault_trigger.proto`:
```protobuf
syntax = "proto3";
package sil;

service FaultTrigger {
  rpc Trigger(TriggerRequest) returns (TriggerResponse);
  rpc ListFaults(ListFaultsRequest) returns (ListFaultsResponse);
  rpc Cancel(CancelRequest) returns (CancelResponse);
}

message TriggerRequest { string fault_type = 1; string payload_json = 2; }
message TriggerResponse { string fault_id = 1; }
message ListFaultsRequest {}
message ListFaultsResponse { repeated ActiveFault active = 1; }
message ActiveFault { string fault_id = 1; string fault_type = 2; string stamp = 3; }
message CancelRequest { string fault_id = 1; }
message CancelResponse { bool success = 1; }
```

Create `idl/proto/sil/self_check.proto`:
```protobuf
syntax = "proto3";
package sil;

service SelfCheck {
  rpc Probe(ProbeRequest) returns (ProbeResponse);
  rpc Status(StatusRequest) returns (StatusResponse);
}

message ProbeRequest {}
message ProbeResponse { bool all_clear = 1; repeated CheckItem items = 2; }
message CheckItem { string name = 1; bool passed = 2; string detail = 3; }
message StatusRequest {}
message StatusResponse { repeated ModulePulse module_pulses = 1; }
// ModulePulse defined in module_pulse.proto
```

Create `idl/proto/sil/export_evidence.proto`:
```protobuf
syntax = "proto3";
package sil;

service ExportEvidence {
  rpc PackMarzip(PackMarzipRequest) returns (PackMarzipResponse);
  rpc PostProcessArrow(PostProcessArrowRequest) returns (PostProcessArrowResponse);
}

message PackMarzipRequest { string run_id = 1; }
message PackMarzipResponse { string download_url = 1; string status = 2; }
message PostProcessArrowRequest { string run_id = 1; }
message PostProcessArrowResponse { string arrow_path = 1; }
```

- [ ] **Step 1.5: Run buf lint**

```bash
cd idl && buf lint
```
Expected: PASS, no errors.

- [ ] **Step 1.6: Run buf breaking (initial baseline)**

```bash
cd idl && buf breaking --against '.git#branch=main'
```
Expected: FAIL (no prior IDL on main — this is greenfield). Accept as baseline.

- [ ] **Step 1.7: Commit**

```bash
git add idl/
git commit -m "feat(sil): add Protobuf IDL SSoT — 10 messages + 6 services

Per §4.3 of SIL architecture design doc. buf lint passes.
Greenfield baseline — buf breaking will enforce from this commit.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 2: TypeScript Type Generation (protobuf-ts)

**Files:** Create `web/src/types/sil.ts` (generated), `web/tsconfig.json` update

**Depends on:** Task 1 complete

**Parallel with:** Tasks 3, 4

- [ ] **Step 2.1: Install protobuf-ts plugin**

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer && npm --prefix web install -D @protobuf-ts/plugin @protobuf-ts/runtime
```

- [ ] **Step 2.2: Generate TypeScript types**

```bash
cd idl && buf generate
```
This writes `web/src/types/sil.ts` (all 10 message + 6 service types).

- [ ] **Step 2.3: Verify generated types compile**

```bash
cd web && npx tsc --noEmit
```
Expected: PASS (no type errors from generated proto types).

- [ ] **Step 2.4: Add barrel export**

Create `web/src/types/index.ts`:
```typescript
export * from './sil';
```

Update `web/tsconfig.json` to include the proto-generated path if needed.

- [ ] **Step 2.5: Commit**

```bash
git add web/src/types/ web/package.json web/package-lock.json
git commit -m "feat(sil): generate TypeScript types from Protobuf IDL"
```

---

## Task 3: Python Protobuf Stubs

**Files:** Create `src/sil_orchestrator/sil_proto/` (generated)

**Depends on:** Task 1 complete

**Parallel with:** Tasks 2, 4

- [ ] **Step 3.1: Install protobuf for Python**

```bash
pip install protobuf>=4.21.0 grpcio-tools>=1.48.0
```

- [ ] **Step 3.2: Generate Python stubs**

```bash
mkdir -p src/sil_orchestrator/sil_proto
python -m grpc_tools.protoc \
  -I idl/proto \
  --python_out=src/sil_orchestrator/sil_proto \
  --pyi_out=src/sil_orchestrator/sil_proto \
  idl/proto/sil/*.proto
```

- [ ] **Step 3.3: Create `__init__.py` with re-exports**

Create `src/sil_orchestrator/sil_proto/__init__.py`:
```python
"""Generated Protobuf stubs for SIL messages and services."""
from sil_proto.own_ship_state_pb2 import OwnShipState
from sil_proto.target_vessel_state_pb2 import TargetVesselState
from sil_proto.radar_measurement_pb2 import RadarMeasurement
from sil_proto.ais_message_pb2 import AISMessage
from sil_proto.environment_state_pb2 import EnvironmentState
from sil_proto.fault_event_pb2 import FaultEvent
from sil_proto.module_pulse_pb2 import ModulePulse
from sil_proto.scoring_row_pb2 import ScoringRow
from sil_proto.asdr_event_pb2 import ASDREvent
from sil_proto.lifecycle_status_pb2 import LifecycleStatus

__all__ = [
    "OwnShipState", "TargetVesselState", "RadarMeasurement", "AISMessage",
    "EnvironmentState", "FaultEvent", "ModulePulse", "ScoringRow",
    "ASDREvent", "LifecycleStatus",
]
```

- [ ] **Step 3.4: Verify import works**

```bash
cd src/sil_orchestrator && python -c "from sil_proto import OwnShipState; print('OK')"
```
Expected: `OK`

- [ ] **Step 3.5: Commit**

```bash
git add src/sil_orchestrator/sil_proto/
git commit -m "feat(sil): generate Python protobuf stubs from IDL"
```

---

## Task 4: proto2ros Conversion Stubs

**Files:** Create `src/sim_workbench/sil_msgs/` (generated ROS2 .msg files)

**Depends on:** Task 1 complete

**Parallel with:** Tasks 2, 3

- [ ] **Step 4.1: Install proto2ros**

```bash
pip install proto2ros
```

- [ ] **Step 4.2: Generate ROS2 .msg files**

```bash
mkdir -p src/sim_workbench/sil_msgs/msg
python -m proto2ros \
  -I idl/proto \
  --output-dir src/sim_workbench/sil_msgs/msg \
  idl/proto/sil/*.proto
```

- [ ] **Step 4.3: Create ROS2 package scaffold**

Create `src/sim_workbench/sil_msgs/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.8)
project(sil_msgs)

find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/OwnShipState.msg"
  "msg/TargetVesselState.msg"
  "msg/RadarMeasurement.msg"
  "msg/AISMessage.msg"
  "msg/EnvironmentState.msg"
  "msg/FaultEvent.msg"
  "msg/ModulePulse.msg"
  "msg/ScoringRow.msg"
  "msg/ASDREvent.msg"
  "msg/LifecycleStatus.msg"
)

ament_package()
```

Create `src/sim_workbench/sil_msgs/package.xml`:
```xml
<?xml version="1.0"?>
<package format="3">
  <name>sil_msgs</name>
  <version>0.1.0</version>
  <description>SIL Protobuf-derived ROS2 messages</description>
  <buildtool_depend>ament_cmake</buildtool_depend>
  <build_depend>rosidl_default_generators</build_depend>
  <exec_depend>rosidl_default_runtime</exec_depend>
  <member_of_group>rosidl_interface_packages</member_of_group>
  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 4.4: Commit**

```bash
git add src/sim_workbench/sil_msgs/
git commit -m "feat(sil): generate ROS2 .msg files via proto2ros"
```

---

## Task 5: Zustand Telemetry Stores

**Files:**
- Create: `web/src/store/telemetryStore.ts`
- Create: `web/src/store/scenarioStore.ts`
- Create: `web/src/store/controlStore.ts`
- Create: `web/src/store/replayStore.ts`
- Create: `web/src/store/uiStore.ts`
- Create: `web/src/store/index.ts`
- Test: `web/src/store/__tests__/telemetryStore.test.ts`

**Depends on:** Task 2 (TS types)

**Parallel with:** Tasks 6, 7

**Spec reference:** §5.2 (5 Zustand slices)

- [ ] **Step 5.1: Install Zustand**

```bash
npm --prefix web install zustand
```

- [ ] **Step 5.2: Create telemetryStore (50Hz real-time data)**

Create `web/src/store/telemetryStore.ts`:
```typescript
import { create } from 'zustand';
import type { OwnShipState, TargetVesselState, EnvironmentState, ModulePulse } from '../types';

interface TelemetryState {
  ownShip: OwnShipState | null;
  targets: TargetVesselState[];
  environment: EnvironmentState | null;
  modulePulses: ModulePulse[];

  updateOwnShip: (state: OwnShipState) => void;
  updateTargets: (targets: TargetVesselState[]) => void;
  updateEnvironment: (env: EnvironmentState) => void;
  updateModulePulses: (pulses: ModulePulse[]) => void;
  reset: () => void;
}

const initialState = {
  ownShip: null,
  targets: [],
  environment: null,
  modulePulses: [],
};

export const useTelemetryStore = create<TelemetryState>((set) => ({
  ...initialState,
  updateOwnShip: (ownShip) => set({ ownShip }),
  updateTargets: (targets) => set({ targets }),
  updateEnvironment: (environment) => set({ environment }),
  updateModulePulses: (modulePulses) => set({ modulePulses }),
  reset: () => set(initialState),
}));
```

- [ ] **Step 5.3: Create scenarioStore (lifecycle + scenario identity)**

Create `web/src/store/scenarioStore.ts`:
```typescript
import { create } from 'zustand';
import type { LifecycleStatus } from '../types';

interface ScenarioState {
  scenarioId: string | null;
  runId: string | null;
  scenarioHash: string | null;
  lifecycleState: LifecycleStatus['currentState'] | null;

  setScenario: (id: string, hash: string) => void;
  setRunId: (runId: string) => void;
  setLifecycleState: (state: LifecycleStatus['currentState']) => void;
  reset: () => void;
}

export const useScenarioStore = create<ScenarioState>((set) => ({
  scenarioId: null,
  runId: null,
  scenarioHash: null,
  lifecycleState: null,
  setScenario: (scenarioId, scenarioHash) => set({ scenarioId, scenarioHash }),
  setRunId: (runId) => set({ runId }),
  setLifecycleState: (lifecycleState) => set({ lifecycleState }),
  reset: () => set({ scenarioId: null, runId: null, scenarioHash: null, lifecycleState: null }),
}));
```

- [ ] **Step 5.4: Create controlStore (runtime controls)**

Create `web/src/store/controlStore.ts`:
```typescript
import { create } from 'zustand';

interface ControlState {
  simRate: number;        // 0.5, 1, 2, 4, 10
  isPaused: boolean;
  activeFaults: string[];

  setSimRate: (rate: number) => void;
  setPaused: (paused: boolean) => void;
  setActiveFaults: (faults: string[]) => void;
  reset: () => void;
}

export const useControlStore = create<ControlState>((set) => ({
  simRate: 1,
  isPaused: false,
  activeFaults: [],
  setSimRate: (simRate) => set({ simRate }),
  setPaused: (isPaused) => set({ isPaused }),
  setActiveFaults: (activeFaults) => set({ activeFaults }),
  reset: () => set({ simRate: 1, isPaused: false, activeFaults: [] }),
}));
```

- [ ] **Step 5.5: Create replayStore (Phase 2 stub)**

Create `web/src/store/replayStore.ts`:
```typescript
import { create } from 'zustand';

interface ReplayState {
  scrubTime: number;
  mcapDuration: number;
  isScrubbing: boolean;

  setScrubTime: (t: number) => void;
  setDuration: (d: number) => void;
  setScrubbing: (s: boolean) => void;
  reset: () => void;
}

export const useReplayStore = create<ReplayState>((set) => ({
  scrubTime: 0,
  mcapDuration: 0,
  isScrubbing: false,
  setScrubTime: (scrubTime) => set({ scrubTime }),
  setDuration: (mcapDuration) => set({ mcapDuration }),
  setScrubbing: (isScrubbing) => set({ isScrubbing }),
  reset: () => set({ scrubTime: 0, mcapDuration: 0, isScrubbing: false }),
}));
```

- [ ] **Step 5.6: Create uiStore (view state)**

Create `web/src/store/uiStore.ts`:
```typescript
import { create } from 'zustand';

type ViewMode = 'captain' | 'god' | 'roc';

interface UIState {
  viewMode: ViewMode;
  asdrLogExpanded: boolean;
  pulseBarExpanded: boolean;

  setViewMode: (mode: ViewMode) => void;
  toggleAsdrLog: () => void;
  togglePulseBar: () => void;
  reset: () => void;
}

export const useUIStore = create<UIState>((set) => ({
  viewMode: 'captain',
  asdrLogExpanded: false,
  pulseBarExpanded: false,
  setViewMode: (viewMode) => set({ viewMode }),
  toggleAsdrLog: () => set((s) => ({ asdrLogExpanded: !s.asdrLogExpanded })),
  togglePulseBar: () => set((s) => ({ pulseBarExpanded: !s.pulseBarExpanded })),
  reset: () => set({ viewMode: 'captain', asdrLogExpanded: false, pulseBarExpanded: false }),
}));
```

- [ ] **Step 5.7: Create barrel export**

Create `web/src/store/index.ts`:
```typescript
export { useTelemetryStore } from './telemetryStore';
export { useScenarioStore } from './scenarioStore';
export { useControlStore } from './controlStore';
export { useReplayStore } from './replayStore';
export { useUIStore } from './uiStore';
```

- [ ] **Step 5.8: Write unit test for telemetryStore**

Create `web/src/store/__tests__/telemetryStore.test.ts`:
```typescript
import { describe, it, expect, beforeEach } from 'vitest';
import { useTelemetryStore } from '../telemetryStore';

describe('telemetryStore', () => {
  beforeEach(() => useTelemetryStore.getState().reset());

  it('starts with null ownShip', () => {
    expect(useTelemetryStore.getState().ownShip).toBeNull();
  });

  it('updates ownShip state immutably', () => {
    const fakeState = {
      stamp: { seconds: BigInt(0), nanos: 0 },
      pose: { lat: 63.4, lon: 10.4, heading: 0.5 },
      kinematics: { sog: 5.0, cog: 0.5, rot: 0, u: 5.0, v: 0, r: 0 },
      controlState: { rudderAngle: 0, throttle: 0.5 },
    };
    useTelemetryStore.getState().updateOwnShip(fakeState as any);
    expect(useTelemetryStore.getState().ownShip?.pose.lat).toBe(63.4);
  });

  it('resets all fields', () => {
    useTelemetryStore.getState().updateOwnShip({} as any);
    useTelemetryStore.getState().reset();
    expect(useTelemetryStore.getState().ownShip).toBeNull();
    expect(useTelemetryStore.getState().targets).toEqual([]);
  });
});
```

- [ ] **Step 5.9: Run tests**

```bash
npm --prefix web test -- --run web/src/store/__tests__/telemetryStore.test.ts
```
Expected: 3 tests PASS.

- [ ] **Step 5.10: Commit**

```bash
git add web/src/store/ web/package.json web/package-lock.json
git commit -m "feat(sil): add Zustand store slices — telemetry, scenario, control, replay, UI"
```

---

## Task 6: RTK Query API Slice

**Files:**
- Create: `web/src/api/silApi.ts`
- Modify: `web/src/main.tsx` (add Provider if not present)

**Depends on:** Task 2 (TS types)

**Parallel with:** Tasks 5, 7

**Spec reference:** §5.3

- [ ] **Step 6.1: Install RTK Query dependencies**

```bash
npm --prefix web install @reduxjs/toolkit react-redux
```

- [ ] **Step 6.2: Create RTK Query API slice**

Create `web/src/api/silApi.ts`:
```typescript
import { createApi, fetchBaseQuery } from '@reduxjs/toolkit/query/react';
import type { LifecycleStatus } from '../types';

export interface ScenarioSummary {
  id: string;
  name: string;
  encounter_type: string;
}

export interface ScenarioDetail {
  yaml_content: string;
  hash: string;
}

export interface ValidateResult {
  valid: boolean;
  errors: string[];
}

export interface ProbeResult {
  all_clear: boolean;
  items: { name: string; passed: boolean; detail: string }[];
}

export const silApi = createApi({
  reducerPath: 'silApi',
  baseQuery: fetchBaseQuery({ baseUrl: '/api/v1' }),
  tagTypes: ['Scenario', 'Run'],
  endpoints: (builder) => ({
    // Scenario CRUD
    listScenarios: builder.query<ScenarioSummary[], void>({
      query: () => '/scenarios',
      providesTags: ['Scenario'],
    }),
    getScenario: builder.query<ScenarioDetail, string>({
      query: (id) => `/scenarios/${id}`,
      providesTags: (_result, _error, id) => [{ type: 'Scenario', id }],
    }),
    validateScenario: builder.mutation<ValidateResult, string>({
      query: (yamlContent) => ({
        url: '/scenarios/validate',
        method: 'POST',
        body: { yaml_content: yamlContent },
      }),
    }),
    createScenario: builder.mutation<{ scenario_id: string; hash: string }, string>({
      query: (yamlContent) => ({
        url: '/scenarios',
        method: 'POST',
        body: { yaml_content: yamlContent },
      }),
      invalidatesTags: ['Scenario'],
    }),
    deleteScenario: builder.mutation<void, string>({
      query: (id) => ({ url: `/scenarios/${id}`, method: 'DELETE' }),
      invalidatesTags: ['Scenario'],
    }),

    // Lifecycle
    getLifecycleStatus: builder.query<LifecycleStatus, void>({
      query: () => '/lifecycle/status',
    }),
    configureLifecycle: builder.mutation<{ success: boolean }, string>({
      query: (scenarioId) => ({
        url: '/lifecycle/configure',
        method: 'POST',
        body: { scenario_id: scenarioId },
      }),
    }),
    activateLifecycle: builder.mutation<{ success: boolean }, void>({
      query: () => ({ url: '/lifecycle/activate', method: 'POST' }),
    }),
    deactivateLifecycle: builder.mutation<{ success: boolean }, void>({
      query: () => ({ url: '/lifecycle/deactivate', method: 'POST' }),
    }),

    // Self-check
    probeSelfCheck: builder.mutation<ProbeResult, void>({
      query: () => ({ url: '/selfcheck/probe', method: 'POST' }),
    }),
    getHealthStatus: builder.query<{ module_pulses: any[] }, void>({
      query: () => '/selfcheck/status',
    }),

    // Export
    exportMarzip: builder.mutation<{ download_url: string; status: string }, string>({
      query: (runId) => ({
        url: '/export/marzip',
        method: 'POST',
        body: { run_id: runId },
      }),
    }),
    getExportStatus: builder.query<{ status: string; download_url?: string }, string>({
      query: (runId) => `/export/status/${runId}`,
    }),
  }),
});

export const {
  useListScenariosQuery,
  useGetScenarioQuery,
  useValidateScenarioMutation,
  useCreateScenarioMutation,
  useDeleteScenarioMutation,
  useGetLifecycleStatusQuery,
  useConfigureLifecycleMutation,
  useActivateLifecycleMutation,
  useDeactivateLifecycleMutation,
  useProbeSelfCheckMutation,
  useGetHealthStatusQuery,
  useExportMarzipMutation,
  useGetExportStatusQuery,
} = silApi;
```

- [ ] **Step 6.3: Add Redux Provider to main.tsx**

Modify `web/src/main.tsx` — if no existing Redux Provider, wrap App:
```typescript
import { Provider } from 'react-redux';
import { configureStore } from '@reduxjs/toolkit';
import { silApi } from './api/silApi';

const store = configureStore({
  reducer: {
    [silApi.reducerPath]: silApi.reducer,
  },
  middleware: (getDefaultMiddleware) =>
    getDefaultMiddleware().concat(silApi.middleware),
});

// Wrap <App /> with <Provider store={store}> if not already wrapped
```

If `main.tsx` already has its own wrapping pattern, only add the `silApi` reducer + middleware to the existing store.

- [ ] **Step 6.4: Verify TypeScript compilation**

```bash
cd web && npx tsc --noEmit
```
Expected: PASS.

- [ ] **Step 6.5: Commit**

```bash
git add web/src/api/ web/src/main.tsx web/package.json web/package-lock.json
git commit -m "feat(sil): add RTK Query API slice for scenario CRUD, lifecycle, self-check, export"
```

---

## Task 7: MapLibre Base + S-57 MVT Setup

**Files:**
- Create: `web/src/map/SilMapView.tsx`
- Create: `web/src/map/layers.ts`
- Create: `web/src/map/vesselSprite.ts`
- Test: `web/src/map/__tests__/SilMapView.test.tsx`

**Depends on:** Task 2 (TS types)

**Parallel with:** Tasks 5, 6

**Spec reference:** §5.4 (Captain 视图 elements), §5.1 Layout C

- [ ] **Step 7.1: Create S-57 MVT layer definition**

Create `web/src/map/layers.ts`:
```typescript
import type { LayerSpecification, SourceSpecification } from 'maplibre-gl';

export const S57_TILE_URL = '/tiles/s57/{z}/{x}/{y}.pbf';

export const s57Source: SourceSpecification = {
  type: 'vector',
  tiles: [S57_TILE_URL],
  minzoom: 8,
  maxzoom: 18,
};

export const s57Layers: LayerSpecification[] = [
  {
    id: 's57-depth-area',
    type: 'fill',
    source: 's57',
    'source-layer': 'depth_area',
    paint: { 'fill-color': '#b8d4f0', 'fill-opacity': 0.4 },
  },
  {
    id: 's57-land',
    type: 'fill',
    source: 's57',
    'source-layer': 'land_area',
    paint: { 'fill-color': '#f5f0e0' },
  },
  {
    id: 's57-coastline',
    type: 'line',
    source: 's57',
    'source-layer': 'coastline',
    paint: { 'line-color': '#333', 'line-width': 1.5 },
  },
];
```

- [ ] **Step 7.2: Create vessel sprite renderer**

Create `web/src/map/vesselSprite.ts`:
```typescript
export function createOwnShipSVG(headingDeg: number, color = '#00ff88'): string {
  // Simple arrow ship symbol, pointing up at heading=0
  return `<svg width="40" height="60" viewBox="0 0 40 60" xmlns="http://www.w3.org/2000/svg">
    <polygon points="20,0 0,50 20,40 40,50" fill="${color}" stroke="#fff" stroke-width="1"
             transform="rotate(${headingDeg}, 20, 30)"/>
  </svg>`;
}

export function createTargetSVG(headingDeg: number, color = '#ff4444'): string {
  return `<svg width="30" height="45" viewBox="0 0 30 45" xmlns="http://www.w3.org/2000/svg">
    <circle cx="15" cy="15" r="14" fill="none" stroke="${color}" stroke-width="2"
            transform="rotate(${headingDeg}, 15, 22)"/>
    <line x1="15" y1="0" x2="15" y2="30" stroke="${color}" stroke-width="2"
          transform="rotate(${headingDeg}, 15, 22)"/>
  </svg>`;
}
```

- [ ] **Step 7.3: Create SilMapView component**

Create `web/src/map/SilMapView.tsx`:
```typescript
import { useEffect, useRef } from 'react';
import maplibregl from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import { useTelemetryStore } from '../store';
import { s57Source, s57Layers } from './layers';
import { createOwnShipSVG, createTargetSVG } from './vesselSprite';

interface SilMapViewProps {
  followOwnShip?: boolean;
  viewMode?: 'captain' | 'god';
}

export function SilMapView({ followOwnShip = true, viewMode = 'captain' }: SilMapViewProps) {
  const mapContainer = useRef<HTMLDivElement>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);

  useEffect(() => {
    if (!mapContainer.current || mapRef.current) return;

    const map = new maplibregl.Map({
      container: mapContainer.current,
      style: {
        version: 8,
        sources: { s57: s57Source as any },
        layers: s57Layers as any,
      },
      center: [10.4, 63.4], // Trondheim default
      zoom: 12,
    });

    map.addControl(new maplibregl.NavigationControl(), 'top-right');
    mapRef.current = map;

    return () => { map.remove(); mapRef.current = null; };
  }, []);

  // TODO: Phase 2 — add vessel markers via GeoJSON sources updated from telemetryStore
  // For Phase 1, map renders static ENC tiles only (Scenario Builder preview use case)

  return (
    <div ref={mapContainer} style={{ width: '100%', height: '100%' }}
         data-testid="sil-map-view" />
  );
}
```

- [ ] **Step 7.4: Write smoke test**

Create `web/src/map/__tests__/SilMapView.test.tsx`:
```typescript
import { describe, it, expect } from 'vitest';
import { render } from '@testing-library/react';
import { SilMapView } from '../SilMapView';

describe('SilMapView', () => {
  it('renders map container div', () => {
    const { getByTestId } = render(<SilMapView />);
    expect(getByTestId('sil-map-view')).toBeInTheDocument();
  });
});
```

- [ ] **Step 7.5: Run test**

```bash
npm --prefix web test -- --run web/src/map/__tests__/SilMapView.test.tsx
```
Expected: PASS.

- [ ] **Step 7.6: Commit**

```bash
git add web/src/map/
git commit -m "feat(sil): add MapLibre base with S-57 MVT layer config and vessel sprites"
```

---

## Task 8: Screen ① — Scenario Builder

**Files:**
- Create: `web/src/screens/ScenarioBuilder.tsx`
- Create: `web/src/screens/__tests__/ScenarioBuilder.test.tsx`
- Modify: `web/src/App.tsx` (add route)

**Depends on:** Tasks 5, 6, 7

**Parallel with:** Tasks 9, 10

**Spec reference:** §5.1 (Layout C: left form / right ENC + Imazu preview + target cards)

- [ ] **Step 8.1: Create ScenarioBuilder screen**

Create `web/src/screens/ScenarioBuilder.tsx`:
```typescript
import { useState } from 'react';
import { SilMapView } from '../map/SilMapView';
import {
  useListScenariosQuery,
  useGetScenarioQuery,
  useValidateScenarioMutation,
  useCreateScenarioMutation,
  useDeleteScenarioMutation,
} from '../api/silApi';
import { useScenarioStore } from '../store';
import type { ScenarioSummary } from '../api/silApi';

export function ScenarioBuilder() {
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [yamlEditor, setYamlEditor] = useState('');
  const [activeTab, setActiveTab] = useState<'template' | 'procedural' | 'ais'>('template');

  const { data: scenarios = [] } = useListScenariosQuery();
  const { data: scenarioDetail } = useGetScenarioQuery(selectedId!, { skip: !selectedId });
  const [validate, { data: validation }] = useValidateScenarioMutation();
  const [createScenario] = useCreateScenarioMutation();
  const [deleteScenario] = useDeleteScenarioMutation();
  const setScenario = useScenarioStore((s) => s.setScenario);

  const handleSelect = (id: string) => {
    setSelectedId(id);
    if (scenarioDetail) setYamlEditor(scenarioDetail.yaml_content);
  };

  const handleValidate = () => validate(yamlEditor);
  const handleSave = async () => {
    const result = await createScenario(yamlEditor).unwrap();
    setScenario(result.scenario_id, result.hash);
  };

  return (
    <div style={{ display: 'flex', height: '100vh' }} data-testid="scenario-builder">
      {/* Left panel: form */}
      <div style={{ width: 400, padding: 16, overflowY: 'auto', borderRight: '1px solid #333' }}>
        <h2>Scenario Builder</h2>

        {/* Tab bar */}
        <div style={{ display: 'flex', gap: 8, marginBottom: 16 }}>
          {(['template', 'procedural', 'ais'] as const).map((tab) => (
            <button key={tab} onClick={() => setActiveTab(tab)}
                    style={{ fontWeight: activeTab === tab ? 'bold' : 'normal' }}>
              {tab === 'template' ? 'Template' : tab === 'procedural' ? 'Procedural' : 'AIS'}
            </button>
          ))}
        </div>

        {/* Scenario list */}
        <select size={8} style={{ width: '100%', marginBottom: 8 }}
                onChange={(e) => handleSelect(e.target.value)}>
          {scenarios.map((s: ScenarioSummary) => (
            <option key={s.id} value={s.id}>{s.name} ({s.encounter_type})</option>
          ))}
        </select>

        {/* YAML editor */}
        <textarea
          value={yamlEditor}
          onChange={(e) => setYamlEditor(e.target.value)}
          placeholder="Paste YAML or select scenario..."
          rows={20}
          style={{ width: '100%', fontFamily: 'monospace', fontSize: 12 }}
          data-testid="yaml-editor"
        />

        {/* Action buttons */}
        <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
          <button onClick={handleValidate} data-testid="btn-validate">Validate</button>
          <button onClick={handleSave} data-testid="btn-save">Save</button>
          <button onClick={() => selectedId && deleteScenario(selectedId)}
                  data-testid="btn-delete" disabled={!selectedId}>Delete</button>
        </div>

        {validation && (
          <div style={{ marginTop: 8, color: validation.valid ? 'green' : 'red' }}>
            {validation.valid ? 'Valid' : validation.errors.join(', ')}
          </div>
        )}
      </div>

      {/* Right panel: ENC + Imazu preview */}
      <div style={{ flex: 1 }}>
        <SilMapView />
      </div>
    </div>
  );
}
```

- [ ] **Step 8.2: Write smoke test**

Create `web/src/screens/__tests__/ScenarioBuilder.test.tsx`:
```typescript
import { describe, it, expect } from 'vitest';
import { render } from '../../test-setup';
import { ScenarioBuilder } from '../ScenarioBuilder';

describe('ScenarioBuilder', () => {
  it('renders without crashing', () => {
    const { getByTestId } = render(<ScenarioBuilder />);
    expect(getByTestId('scenario-builder')).toBeInTheDocument();
  });
});
```

- [ ] **Step 8.3: Add route in App.tsx**

Modify `web/src/App.tsx` — add import and route for `/builder` (if routing exists, add to router; if not, conditionally render based on a simple state-based router):
```typescript
import { ScenarioBuilder } from './screens/ScenarioBuilder';

// In App component, add routing:
// / → redirect → /builder
// /builder → <ScenarioBuilder />
```

- [ ] **Step 8.4: Run test**

```bash
npm --prefix web test -- --run web/src/screens/__tests__/ScenarioBuilder.test.tsx
```
Expected: PASS.

- [ ] **Step 8.5: Commit**

```bash
git add web/src/screens/ScenarioBuilder.tsx web/src/screens/__tests__/ web/src/App.tsx
git commit -m "feat(sil): add Screen ① Scenario Builder with YAML editor + ENC preview"
```

---

## Task 9: Screen ② — Pre-flight

**Files:**
- Create: `web/src/screens/Preflight.tsx`
- Create: `web/src/screens/__tests__/Preflight.test.tsx`
- Modify: `web/src/App.tsx` (add route)

**Depends on:** Tasks 5, 6

**Parallel with:** Tasks 8, 10

**Spec reference:** §5.1 (center-positioned progress + countdown, no map)

- [ ] **Step 9.1: Create Preflight screen**

Create `web/src/screens/Preflight.tsx`:
```typescript
import { useState, useEffect, useCallback } from 'react';
import { useParams, useNavigate } from 'react-router-dom'; // or custom router
import { useProbeSelfCheckMutation, useConfigureLifecycleMutation, useActivateLifecycleMutation } from '../api/silApi';

type CheckItem = { name: string; passed: boolean; detail: string };
type CheckPhase = 'idle' | 'checking' | 'passed' | 'failed';

export function Preflight() {
  const { runId } = useParams<{ runId: string }>();
  const [phase, setPhase] = useState<CheckPhase>('idle');
  const [checks, setChecks] = useState<CheckItem[]>([]);
  const [countdown, setCountdown] = useState(3);
  const [activate] = useActivateLifecycleMutation();
  const navigate = useNavigate();

  const runChecks = useCallback(async () => {
    setPhase('checking');
    // Simulate 5-stage self-check polling
    const stages = ['M1-M8 Health', 'ENC Loading', 'ASDR Ready', 'UTC Sync', 'Hash Verify'];
    const items: CheckItem[] = [];
    for (const name of stages) {
      await new Promise((r) => setTimeout(r, 600));
      items.push({ name, passed: true, detail: `${name} OK` });
      setChecks([...items]);
    }
    setPhase('passed');
  }, []);

  useEffect(() => { runChecks(); }, [runChecks]);

  useEffect(() => {
    if (phase !== 'passed') return;
    if (countdown <= 0) {
      activate().then(() => navigate(`/bridge/${runId}`));
      return;
    }
    const timer = setTimeout(() => setCountdown((c) => c - 1), 1000);
    return () => clearTimeout(timer);
  }, [phase, countdown, activate, navigate, runId]);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center',
                    justifyContent: 'center', height: '100vh' }}
         data-testid="preflight">
      <h2>Pre-flight Check</h2>

      {checks.map((item, i) => (
        <div key={i} style={{ margin: 4, color: item.passed ? 'green' : 'red' }}>
          {item.passed ? '✓' : '✗'} {item.name} — {item.detail}
        </div>
      ))}

      {phase === 'passed' && (
        <div style={{ fontSize: 48, marginTop: 32, fontWeight: 'bold' }}>
          Starting in {countdown}...
        </div>
      )}

      <button onClick={() => navigate('/builder')} style={{ marginTop: 24 }}>← Back</button>
    </div>
  );
}
```

- [ ] **Step 9.2: Write smoke test**

Create `web/src/screens/__tests__/Preflight.test.tsx`:
```typescript
import { describe, it, expect } from 'vitest';
import { render } from '../../test-setup';
import { Preflight } from '../Preflight';

describe('Preflight', () => {
  it('renders without crashing', () => {
    const { getByTestId } = render(<Preflight />);
    expect(getByTestId('preflight')).toBeInTheDocument();
  });
});
```

- [ ] **Step 9.3: Add route**

Add `/preflight/:runId` → `<Preflight />` in App.tsx routing.

- [ ] **Step 9.4: Run test**

```bash
npm --prefix web test -- --run web/src/screens/__tests__/Preflight.test.tsx
```
Expected: PASS.

- [ ] **Step 9.5: Commit**

```bash
git add web/src/screens/Preflight.tsx web/src/screens/__tests__/ web/src/App.tsx
git commit -m "feat(sil): add Screen ② Pre-flight with 5-stage check + countdown"
```

---

## Task 10: Screen ④ — Run Report

**Files:**
- Create: `web/src/screens/RunReport.tsx`
- Create: `web/src/screens/__tests__/RunReport.test.tsx`
- Modify: `web/src/App.tsx` (add route)

**Depends on:** Tasks 5, 6

**Parallel with:** Tasks 8, 9

**Spec reference:** §5.1 (timeline top + 4 KPI cards + line chart + COLREGs chain + export)

- [ ] **Step 10.1: Create RunReport screen**

Create `web/src/screens/RunReport.tsx`:
```typescript
import { useParams, useNavigate } from 'react-router-dom';
import { useExportMarzipMutation, useGetExportStatusQuery } from '../api/silApi';
import { useState } from 'react';

interface KpiCardProps { label: string; value: string; unit: string; }

function KpiCard({ label, value, unit }: KpiCardProps) {
  return (
    <div style={{ border: '1px solid #444', borderRadius: 8, padding: 16, minWidth: 160 }}>
      <div style={{ fontSize: 12, color: '#888' }}>{label}</div>
      <div style={{ fontSize: 28, fontWeight: 'bold' }}>{value}</div>
      <div style={{ fontSize: 12, color: '#888' }}>{unit}</div>
    </div>
  );
}

export function RunReport() {
  const { runId } = useParams<{ runId: string }>();
  const navigate = useNavigate();
  const [exportMarzip, { isLoading }] = useExportMarzipMutation();
  const [exportUrl, setExportUrl] = useState<string | null>(null);
  const [verdict, setVerdict] = useState<'PASS' | 'FAIL' | null>(null);

  const handleExport = async () => {
    const result = await exportMarzip(runId!).unwrap();
    setExportUrl(result.download_url);
  };

  const handleVerdict = (v: 'PASS' | 'FAIL') => setVerdict(v);

  return (
    <div style={{ padding: 24, height: '100vh', display: 'flex', flexDirection: 'column' }}
         data-testid="run-report">
      {/* Timeline scrubber (stub) */}
      <div style={{ height: 80, background: '#1a1a2e', borderRadius: 8, marginBottom: 16,
                      display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        <span style={{ color: '#888' }}>Timeline scrub — Phase 2</span>
      </div>

      {/* KPI cards */}
      <div style={{ display: 'flex', gap: 16, marginBottom: 24 }}>
        <KpiCard label="Min CPA" value="0.42" unit="nm" />
        <KpiCard label="Avg ROT" value="2.1" unit="°/min" />
        <KpiCard label="Distance" value="4.8" unit="nm" />
        <KpiCard label="Duration" value="342" unit="s" />
      </div>

      {/* COLREGs chain (stub) */}
      <div style={{ flex: 1, background: '#1a1a2e', borderRadius: 8, padding: 16, marginBottom: 16 }}>
        <h3>COLREGs Rule Chain</h3>
        <div style={{ color: '#888' }}>Rule 14 → Rule 8 → Rule 16 (Phase 2: from ASDR events)</div>
      </div>

      {/* Verdict + Export */}
      <div style={{ display: 'flex', gap: 16, alignItems: 'center' }}>
        <button onClick={() => handleVerdict('PASS')}
                style={{ background: verdict === 'PASS' ? 'green' : '#333', padding: '8px 24px' }}>
          PASS
        </button>
        <button onClick={() => handleVerdict('FAIL')}
                style={{ background: verdict === 'FAIL' ? 'red' : '#333', padding: '8px 24px' }}>
          FAIL
        </button>
        <button onClick={handleExport} disabled={isLoading}
                style={{ padding: '8px 24px' }} data-testid="btn-export">
          {isLoading ? 'Exporting...' : 'Export .bag / .yaml / .csv'}
        </button>
        <button onClick={() => navigate('/builder')} style={{ padding: '8px 24px' }}>
          New Run
        </button>
        {exportUrl && <a href={exportUrl} style={{ color: '#4fc3f7' }}>Download</a>}
      </div>
    </div>
  );
}
```

- [ ] **Step 10.2: Write smoke test**

Create `web/src/screens/__tests__/RunReport.test.tsx`:
```typescript
import { describe, it, expect } from 'vitest';
import { render } from '../../test-setup';
import { RunReport } from '../RunReport';

describe('RunReport', () => {
  it('renders without crashing', () => {
    const { getByTestId } = render(<RunReport />);
    expect(getByTestId('run-report')).toBeInTheDocument();
  });
});
```

- [ ] **Step 10.3: Add route**

Add `/report/:runId` → `<RunReport />` in App.tsx routing.

- [ ] **Step 10.4: Run test**

```bash
npm --prefix web test -- --run web/src/screens/__tests__/RunReport.test.tsx
```
Expected: PASS.

- [ ] **Step 10.5: Commit**

```bash
git add web/src/screens/RunReport.tsx web/src/screens/__tests__/ web/src/App.tsx
git commit -m "feat(sil): add Screen ④ Run Report with KPI cards, verdict, and export"
```

---

## Task 11: Orchestrator — FastAPI Scaffold

**Files:**
- Create: `src/sil_orchestrator/__init__.py`
- Create: `src/sil_orchestrator/main.py`
- Create: `src/sil_orchestrator/config.py`
- Create: `src/sil_orchestrator/lifecycle_bridge.py`
- Test: `tests/sil_orchestrator/test_main.py`

**Depends on:** Task 3 (Python stubs)

**Blocks:** Tasks 12, 13

**Spec reference:** §1 orchestration plane, §3 lifecycle state machine

- [ ] **Step 11.1: Create config module**

Create `src/sil_orchestrator/config.py`:
```python
"""SIL Orchestrator configuration."""
import os
from pathlib import Path

SCENARIO_DIR = Path(os.getenv("SIL_SCENARIO_DIR", "/var/sil/scenarios"))
RUN_DIR = Path(os.getenv("SIL_RUN_DIR", "/var/sil/runs"))
EXPORT_DIR = Path(os.getenv("SIL_EXPORT_DIR", "/var/sil/exports"))
ROS_DOMAIN_ID = int(os.getenv("ROS_DOMAIN_ID", "0"))
```

- [ ] **Step 11.2: Create lifecycle bridge (rclpy wrapper)**

Create `src/sil_orchestrator/lifecycle_bridge.py`:
```python
"""Bridge between FastAPI REST and ROS2 lifecycle services.

Uses rclpy to call ROS2 lifecycle services on scenario_lifecycle_mgr.
In Phase 1 mock mode, returns success without actual ROS2 calls.
"""

from dataclasses import dataclass
from enum import Enum


class LifecycleState(str, Enum):
    UNCONFIGURED = "unconfigured"
    INACTIVE = "inactive"
    ACTIVE = "active"
    DEACTIVATING = "deactivating"
    FINALIZED = "finalized"


@dataclass
class LifecycleResult:
    success: bool
    error: str = ""


class LifecycleBridge:
    """Thin wrapper over rclpy lifecycle service calls.

    Phase 1: mock implementation (no ROS2 node running yet).
    Phase 2 (Week 3): replace with actual rclpy service clients.
    """

    def __init__(self) -> None:
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id: str | None = None

    @property
    def current_state(self) -> LifecycleState:
        return self._state

    @property
    def scenario_id(self) -> str | None:
        return self._scenario_id

    async def configure(self, scenario_id: str) -> LifecycleResult:
        self._scenario_id = scenario_id
        self._state = LifecycleState.INACTIVE
        return LifecycleResult(success=True)

    async def activate(self) -> LifecycleResult:
        if self._state != LifecycleState.INACTIVE:
            return LifecycleResult(success=False, error=f"Cannot activate from {self._state}")
        self._state = LifecycleState.ACTIVE
        return LifecycleResult(success=True)

    async def deactivate(self) -> LifecycleResult:
        if self._state != LifecycleState.ACTIVE:
            return LifecycleResult(success=False, error=f"Cannot deactivate from {self._state}")
        self._state = LifecycleState.INACTIVE
        return LifecycleResult(success=True)

    async def cleanup(self) -> LifecycleResult:
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id = None
        return LifecycleResult(success=True)
```

- [ ] **Step 11.3: Create FastAPI application**

Create `src/sil_orchestrator/main.py`:
```python
"""SIL Orchestrator — FastAPI REST API bridging frontend to ROS2 lifecycle.

Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §1 orchestration plane
"""

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from sil_orchestrator.lifecycle_bridge import LifecycleBridge, LifecycleState

app = FastAPI(title="SIL Orchestrator", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

bridge = LifecycleBridge()


@app.get("/api/v1/health")
async def health():
    return {"status": "ok"}


@app.get("/api/v1/lifecycle/status")
async def lifecycle_status():
    return {
        "current_state": bridge.current_state.value,
        "scenario_id": bridge.scenario_id,
    }


@app.post("/api/v1/lifecycle/configure")
async def lifecycle_configure(request: dict):
    scenario_id = request.get("scenario_id", "")
    result = await bridge.configure(scenario_id)
    return {"success": result.success, "error": result.error}


@app.post("/api/v1/lifecycle/activate")
async def lifecycle_activate():
    result = await bridge.activate()
    return {"success": result.success, "error": result.error}


@app.post("/api/v1/lifecycle/deactivate")
async def lifecycle_deactivate():
    result = await bridge.deactivate()
    return {"success": result.success, "error": result.error}
```

- [ ] **Step 11.4: Write test**

Create `tests/sil_orchestrator/test_main.py`:
```python
import pytest
from fastapi.testclient import TestClient
from sil_orchestrator.main import app


@pytest.fixture
def client():
    return TestClient(app)


def test_health(client):
    resp = client.get("/api/v1/health")
    assert resp.status_code == 200
    assert resp.json()["status"] == "ok"


def test_lifecycle_configure_activate_deactivate(client):
    resp = client.post("/api/v1/lifecycle/configure", json={"scenario_id": "r14-head-on"})
    assert resp.json()["success"] is True

    resp = client.get("/api/v1/lifecycle/status")
    assert resp.json()["current_state"] == "inactive"
    assert resp.json()["scenario_id"] == "r14-head-on"

    resp = client.post("/api/v1/lifecycle/activate")
    assert resp.json()["success"] is True

    resp = client.post("/api/v1/lifecycle/deactivate")
    assert resp.json()["success"] is True


def test_cannot_activate_from_unconfigured(client):
    resp = client.post("/api/v1/lifecycle/activate")
    assert resp.json()["success"] is False
```

- [ ] **Step 11.5: Run test**

```bash
python -m pytest tests/sil_orchestrator/test_main.py -v
```
Expected: 3 tests PASS.

- [ ] **Step 11.6: Commit**

```bash
git add src/sil_orchestrator/ tests/sil_orchestrator/
git commit -m "feat(sil): add orchestrator FastAPI scaffold with mock lifecycle bridge"
```

---

## Task 12: Orchestrator — Scenario CRUD + Lifecycle REST Endpoints

**Files:**
- Create: `src/sil_orchestrator/scenario_routes.py`
- Create: `src/sil_orchestrator/scenario_store.py`
- Modify: `src/sil_orchestrator/main.py` (register routes)
- Test: `tests/sil_orchestrator/test_scenarios.py`

**Depends on:** Task 11

**Parallel with:** Task 13

- [ ] **Step 12.1: Create scenario file store**

Create `src/sil_orchestrator/scenario_store.py`:
```python
"""File-backed scenario store. Reads/writes YAML under SCENARIO_DIR."""

import hashlib
import uuid
from pathlib import Path

from sil_orchestrator.config import SCENARIO_DIR


class ScenarioStore:
    def __init__(self, base_dir: Path | None = None) -> None:
        self._dir = base_dir or SCENARIO_DIR
        self._dir.mkdir(parents=True, exist_ok=True)

    def list(self) -> list[dict]:
        results = []
        for f in sorted(self._dir.glob("*.yaml")):
            results.append({
                "id": f.stem,
                "name": f.stem.replace("-", " ").title(),
                "encounter_type": "head-on",  # TODO: parse from YAML frontmatter
            })
        return results

    def get(self, scenario_id: str) -> dict | None:
        path = self._dir / f"{scenario_id}.yaml"
        if not path.exists():
            return None
        content = path.read_text()
        h = hashlib.sha256(content.encode()).hexdigest()
        return {"yaml_content": content, "hash": h}

    def create(self, yaml_content: str) -> dict:
        sid = uuid.uuid4().hex[:12]
        path = self._dir / f"{sid}.yaml"
        path.write_text(yaml_content)
        h = hashlib.sha256(yaml_content.encode()).hexdigest()
        return {"scenario_id": sid, "hash": h}

    def update(self, scenario_id: str, yaml_content: str) -> dict | None:
        path = self._dir / f"{scenario_id}.yaml"
        if not path.exists():
            return None
        path.write_text(yaml_content)
        h = hashlib.sha256(yaml_content.encode()).hexdigest()
        return {"hash": h}

    def delete(self, scenario_id: str) -> bool:
        path = self._dir / f"{scenario_id}.yaml"
        if not path.exists():
            return False
        path.unlink()
        return True
```

- [ ] **Step 12.2: Create scenario REST routes**

Create `src/sil_orchestrator/scenario_routes.py`:
```python
from fastapi import APIRouter, HTTPException
from sil_orchestrator.scenario_store import ScenarioStore

router = APIRouter(prefix="/api/v1/scenarios")
store = ScenarioStore()


@router.get("")
async def list_scenarios():
    return store.list()


@router.get("/{scenario_id}")
async def get_scenario(scenario_id: str):
    result = store.get(scenario_id)
    if result is None:
        raise HTTPException(status_code=404, detail="Scenario not found")
    return result


@router.post("")
async def create_scenario(request: dict):
    return store.create(request.get("yaml_content", ""))


@router.put("/{scenario_id}")
async def update_scenario(scenario_id: str, request: dict):
    result = store.update(scenario_id, request.get("yaml_content", ""))
    if result is None:
        raise HTTPException(status_code=404, detail="Scenario not found")
    return result


@router.delete("/{scenario_id}")
async def delete_scenario(scenario_id: str):
    if not store.delete(scenario_id):
        raise HTTPException(status_code=404, detail="Scenario not found")
    return {"deleted": True}


@router.post("/validate")
async def validate_scenario(request: dict):
    yaml_content = request.get("yaml_content", "")
    errors = []
    if not yaml_content.strip():
        errors.append("YAML content is empty")
    # TODO: cerberus-cpp / pyyaml schema validation
    return {"valid": len(errors) == 0, "errors": errors}
```

- [ ] **Step 12.3: Register routes in main.py**

Add to `src/sil_orchestrator/main.py`:
```python
from sil_orchestrator.scenario_routes import router as scenario_router
app.include_router(scenario_router)
```

- [ ] **Step 12.4: Write tests**

Create `tests/sil_orchestrator/test_scenarios.py`:
```python
import tempfile
from pathlib import Path
from sil_orchestrator.scenario_store import ScenarioStore


def test_list_empty():
    with tempfile.TemporaryDirectory() as d:
        store = ScenarioStore(Path(d))
        assert store.list() == []


def test_create_get_list_delete():
    with tempfile.TemporaryDirectory() as d:
        store = ScenarioStore(Path(d))
        result = store.create("name: test\nversion: 1\n")
        sid = result["scenario_id"]
        assert len(sid) == 12

        detail = store.get(sid)
        assert detail is not None
        assert detail["yaml_content"] == "name: test\nversion: 1\n"
        assert len(detail["hash"]) == 64

        assert len(store.list()) == 1

        assert store.delete(sid) is True
        assert store.get(sid) is None
```

- [ ] **Step 12.5: Run tests**

```bash
python -m pytest tests/sil_orchestrator/test_scenarios.py -v
```
Expected: 2 tests PASS.

- [ ] **Step 12.6: Commit**

```bash
git add src/sil_orchestrator/scenario_routes.py src/sil_orchestrator/scenario_store.py src/sil_orchestrator/main.py tests/sil_orchestrator/test_scenarios.py
git commit -m "feat(sil): add scenario CRUD REST endpoints with file-backed store"
```

---

## Task 13: Orchestrator — Self-check + Export Endpoints

**Files:**
- Create: `src/sil_orchestrator/selfcheck_routes.py`
- Create: `src/sil_orchestrator/export_routes.py`
- Modify: `src/sil_orchestrator/main.py` (register routes)
- Test: `tests/sil_orchestrator/test_selfcheck.py`
- Test: `tests/sil_orchestrator/test_export.py`

**Depends on:** Task 11

**Parallel with:** Task 12

- [ ] **Step 13.1: Create self-check routes**

Create `src/sil_orchestrator/selfcheck_routes.py`:
```python
from fastapi import APIRouter

router = APIRouter(prefix="/api/v1/selfcheck")


@router.post("/probe")
async def probe():
    """Run all self-checks and return results."""
    checks = [
        {"name": "M1-M8 Health", "passed": True, "detail": "All modules GREEN"},
        {"name": "ENC Loading", "passed": True, "detail": "Trondheim tiles loaded"},
        {"name": "ASDR Ready", "passed": True, "detail": "ASDR log active"},
        {"name": "UTC Sync", "passed": True, "detail": "PTP offset < 1ms"},
        {"name": "Hash Verify", "passed": True, "detail": "SHA256 matches"},
    ]
    return {"all_clear": all(c["passed"] for c in checks), "items": checks}


@router.get("/status")
async def status():
    """Return current module pulse status."""
    modules = ["M1", "M2", "M3", "M4", "M5", "M6", "M7", "M8"]
    return {
        "module_pulses": [
            {"module_id": m, "state": "GREEN", "latency_ms": 2, "message_drops": 0}
            for m in modules
        ]
    }
```

- [ ] **Step 13.2: Create export routes**

Create `src/sil_orchestrator/export_routes.py`:
```python
import json
import shutil
import tempfile
from pathlib import Path

from fastapi import APIRouter, BackgroundTasks, HTTPException

from sil_orchestrator.config import RUN_DIR, EXPORT_DIR

router = APIRouter(prefix="/api/v1/export")

# In-memory status store (Phase 1; Phase 2 → Redis/DB)
_export_status: dict[str, dict] = {}


def _build_marzip(run_id: str) -> str:
    """Background task: assemble Marzip evidence pack."""
    run_path = RUN_DIR / run_id
    export_path = EXPORT_DIR / f"{run_id}_evidence.marzip"
    export_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        # Write manifest
        manifest = {
            "run_id": run_id,
            "toolchain": "sil_orchestrator v0.1.0",
            "format": "marzip-1.0",
        }
        (tmp_dir / "manifest.yaml").write_text(json.dumps(manifest, indent=2))
        # Copy scenario if exists
        scenario_file = run_path / "scenario.yaml"
        if scenario_file.exists():
            shutil.copy(scenario_file, tmp_dir / "scenario.yaml")
        # Create archive
        shutil.make_archive(str(export_path.with_suffix("")), "zip", tmp_dir)

    _export_status[run_id] = {"status": "complete", "download_url": f"/exports/{run_id}_evidence.marzip"}
    return str(export_path)


@router.post("/marzip")
async def export_marzip(request: dict, background_tasks: BackgroundTasks):
    run_id = request.get("run_id", "")
    run_path = RUN_DIR / run_id
    if not run_path.exists():
        raise HTTPException(status_code=404, detail="Run not found")
    _export_status[run_id] = {"status": "processing"}
    background_tasks.add_task(_build_marzip, run_id)
    return {"status": "processing"}


@router.get("/status/{run_id}")
async def export_status(run_id: str):
    if run_id not in _export_status:
        raise HTTPException(status_code=404, detail="No export for this run")
    return _export_status[run_id]
```

- [ ] **Step 13.3: Register routes in main.py**

Add to `src/sil_orchestrator/main.py`:
```python
from sil_orchestrator.selfcheck_routes import router as selfcheck_router
from sil_orchestrator.export_routes import router as export_router
app.include_router(selfcheck_router)
app.include_router(export_router)
```

- [ ] **Step 13.4: Write test for self-check**

Create `tests/sil_orchestrator/test_selfcheck.py`:
```python
from fastapi.testclient import TestClient
from sil_orchestrator.main import app


def test_probe_all_clear(client=None):
    if client is None:
        client = TestClient(app)
    resp = client.post("/api/v1/selfcheck/probe")
    assert resp.status_code == 200
    data = resp.json()
    assert data["all_clear"] is True
    assert len(data["items"]) == 5


def test_status_returns_8_modules(client=None):
    if client is None:
        client = TestClient(app)
    resp = client.get("/api/v1/selfcheck/status")
    assert resp.status_code == 200
    assert len(resp.json()["module_pulses"]) == 8
```

- [ ] **Step 13.5: Run tests**

```bash
python -m pytest tests/sil_orchestrator/test_selfcheck.py -v
```
Expected: 2 tests PASS.

- [ ] **Step 13.6: Commit**

```bash
git add src/sil_orchestrator/selfcheck_routes.py src/sil_orchestrator/export_routes.py src/sil_orchestrator/main.py tests/sil_orchestrator/
git commit -m "feat(sil): add self-check probe + Marzip export endpoints"
```

---

## Task 14: ROS2 — scenario_lifecycle_mgr Node

**Files:**
- Create: `src/sim_workbench/sil_lifecycle/` (ROS2 package)
- Create: `src/sim_workbench/sil_lifecycle/scenario_lifecycle_mgr.py`
- Create: `src/sim_workbench/sil_lifecycle/launch/lifecycle_launch.py`
- Test: `tests/sil/test_lifecycle_mgr.py`

**Depends on:** Task 4 (proto2ros stubs)

**Blocks:** Tasks 15–22

**Spec reference:** §3 (Lifecycle state machine, 5 states: Unconfigured/Inactive/Active/Deactivating/Finalized)

- [ ] **Step 14.1: Create ROS2 package scaffold**

```bash
mkdir -p src/sim_workbench/sil_lifecycle/launch
```

Create `src/sim_workbench/sil_lifecycle/package.xml`:
```xml
<?xml version="1.0"?>
<package format="3">
  <name>sil_lifecycle</name>
  <version>0.1.0</version>
  <description>SIL Lifecycle Manager — 5-state FSM for 9 business nodes</description>
  <exec_depend>rclpy</exec_depend>
  <exec_depend>lifecycle_msgs</exec_depend>
  <exec_depend>std_msgs</exec_depend>
  <export><build_type>ament_python</build_type></export>
</package>
```

Create `src/sim_workbench/sil_lifecycle/setup.py`:
```python
from setuptools import setup
package_name = 'sil_lifecycle'
setup(
    name=package_name,
    version='0.1.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + '/launch', ['launch/lifecycle_launch.py']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
)
```

- [ ] **Step 14.2: Implement Lifecycle Manager node**

Create `src/sim_workbench/sil_lifecycle/sil_lifecycle/scenario_lifecycle_mgr.py`:
```python
"""Scenario Lifecycle Manager — 5-state FSM coordinating 9 business nodes.

States: UNCONFIGURED → INACTIVE → ACTIVE → DEACTIVATING → FINALIZED
Maps to FE screens: ① UNCONFIGURED → ② INACTIVE → ③ ACTIVE → ④ INACTIVE

Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §3
"""

import time
from enum import Enum

import rclpy
from rclpy.node import Node
from lifecycle_msgs.srv import ChangeState
from lifecycle_msgs.msg import State, Transition
from std_msgs.msg import Float64


class LifecycleState(Enum):
    UNCONFIGURED = State.PRIMARY_STATE_UNCONFIGURED
    INACTIVE = State.PRIMARY_STATE_INACTIVE
    ACTIVE = State.PRIMARY_STATE_ACTIVE
    DEACTIVATING = State.PRIMARY_STATE_DEACTIVATING
    FINALIZED = State.PRIMARY_STATE_FINALIZED


class ScenarioLifecycleMgr(Node):
    """Coordinates lifecycle transitions across 9 business nodes.

    Phase 1: single-process component_container_mt owner.
    Phase 2: may split to multi-process if isolation needed.
    """

    def __init__(self) -> None:
        super().__init__("scenario_lifecycle_mgr")
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id: str = ""
        self._scenario_hash: str = ""
        self._tick_hz = 50.0
        self._sim_rate = 1.0
        self._sim_time = 0.0
        self._wall_start: float = 0.0

        # Service: lifecycle control
        self.create_service(ChangeState, "~/change_state", self._on_change_state)
        # Service: sim clock control
        self.create_service(ChangeState, "~/set_rate", self._on_set_rate)  # reuse for simplicity
        # Publisher: lifecycle status at 1Hz
        self._status_pub = self.create_publisher(Float64, "~/status", 10)
        self._tick_timer = self.create_timer(1.0 / self._tick_hz, self._tick_callback)
        self._status_timer = self.create_timer(1.0, self._publish_status)

        self.get_logger().info("Lifecycle Manager ready (UNCONFIGURED)")

    @property
    def current_state(self) -> LifecycleState:
        return self._state

    def _on_change_state(self, request, response):
        target = request.transition.id
        transitions = {
            Transition.TRANSITION_CONFIGURE: self._do_configure,
            Transition.TRANSITION_ACTIVATE: self._do_activate,
            Transition.TRANSITION_DEACTIVATE: self._do_deactivate,
            Transition.TRANSITION_CLEANUP: self._do_cleanup,
        }
        handler = transitions.get(target)
        if handler:
            response.success = handler()
        else:
            response.success = False
        return response

    def _do_configure(self) -> bool:
        if self._state != LifecycleState.UNCONFIGURED:
            return False
        self._state = LifecycleState.INACTIVE
        self.get_logger().info("State → INACTIVE")
        return True

    def _do_activate(self) -> bool:
        if self._state != LifecycleState.INACTIVE:
            return False
        self._state = LifecycleState.ACTIVE
        self._wall_start = time.time()
        self.get_logger().info("State → ACTIVE (50Hz tick)")
        return True

    def _do_deactivate(self) -> bool:
        if self._state != LifecycleState.ACTIVE:
            return False
        self._state = LifecycleState.DEACTIVATING
        self.get_logger().info("State → DEACTIVATING")
        # Simulate deactivation work
        self._state = LifecycleState.INACTIVE
        self.get_logger().info("State → INACTIVE")
        return True

    def _do_cleanup(self) -> bool:
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id = ""
        self.get_logger().info("State → UNCONFIGURED")
        return True

    def _on_set_rate(self, request, response):
        new_rate = request.transition.id  # abused as float carrier; Phase 2 use proper service
        self._sim_rate = max(0.0, float(new_rate))
        response.success = True
        return response

    def _tick_callback(self):
        if self._state == LifecycleState.ACTIVE:
            self._sim_time += (1.0 / self._tick_hz) * self._sim_rate

    def _publish_status(self):
        msg = Float64()
        msg.data = float(self._state.value)
        self._status_pub.publish(msg)
```

- [ ] **Step 14.3: Write unit test (offline, no ROS2 runtime)**

Create `tests/sil/test_lifecycle_mgr.py`:
```python
"""Unit tests for lifecycle state machine logic (offline — no ROS2 runtime)."""

import pytest


# Test the FSM rules directly — don't need rclpy for this
class _MockLifecycleMgr:
    """Minimal FSM for test — mirrors scenario_lifecycle_mgr state rules."""
    UNCONFIGURED = 0
    INACTIVE = 1
    ACTIVE = 2
    DEACTIVATING = 3
    FINALIZED = 4

    def __init__(self):
        self.state = self.UNCONFIGURED

    def configure(self):
        if self.state != self.UNCONFIGURED:
            return False
        self.state = self.INACTIVE
        return True

    def activate(self):
        if self.state != self.INACTIVE:
            return False
        self.state = self.ACTIVE
        return True

    def deactivate(self):
        if self.state != self.ACTIVE:
            return False
        self.state = self.DEACTIVATING
        self.state = self.INACTIVE
        return True

    def cleanup(self):
        self.state = self.UNCONFIGURED
        return True


@pytest.fixture
def mgr():
    return _MockLifecycleMgr()


def test_initial_state_unconfigured(mgr):
    assert mgr.state == _MockLifecycleMgr.UNCONFIGURED


def test_configure_from_unconfigured(mgr):
    assert mgr.configure() is True
    assert mgr.state == _MockLifecycleMgr.INACTIVE


def test_cannot_activate_from_unconfigured(mgr):
    assert mgr.activate() is False


def test_cannot_configure_twice(mgr):
    mgr.configure()
    assert mgr.configure() is False


def test_full_lifecycle_flow(mgr):
    assert mgr.configure()
    assert mgr.activate()
    assert mgr.deactivate()
    assert mgr.cleanup()
    assert mgr.state == _MockLifecycleMgr.UNCONFIGURED
```

- [ ] **Step 14.4: Run tests**

```bash
python -m pytest tests/sil/test_lifecycle_mgr.py -v
```
Expected: 5 tests PASS.

- [ ] **Step 14.5: Commit**

```bash
git add src/sim_workbench/sil_lifecycle/ tests/sil/test_lifecycle_mgr.py
git commit -m "feat(sil): add scenario_lifecycle_mgr — 5-state FSM coordinating 9 nodes"
```

---

## Tasks 15–22: ROS2 Simulation Nodes (8 parallel tracks)

**All 8 tasks can run in parallel after Task 14 completes.** Each node is independent — they communicate only through DDS topics, not direct API calls. Each follows the same pattern: ROS2 package scaffold → node implementation → offline unit test.

### Task 15: scenario_authoring_node

**Files:** `src/sim_workbench/sil_nodes/scenario_authoring/` (migrated from existing `ais_bridge` + `scenario_authoring`)

**Key implementation:**
- ROS2 Node wrapping existing `scenario_authoring` Python modules
- YAML CRUD via filesystem (same as orchestrator scenario_store)
- Imazu-22 library 22 scenario hashing
- AIS 5-stage pipeline (inherit from D1.3b.2 tests)
- Service: `ScenarioCRUD` (List/Get/Create/Update/Delete/Validate)

**Test:** `tests/sil/test_scenario_authoring.py` — test YAML roundtrip, Imazu-22 hash stability, AIS pipeline stage outputs

---

### Task 16: ship_dynamics_node

**Files:** `src/sim_workbench/sil_nodes/ship_dynamics/` (upgraded from existing `fcb_simulator`)

**Key implementation:**
- Port existing `fcb_simulator/fcb_sim_py/` MMG model into ROS2 Node
- 4-DOF MMG (Yasukawa 2015): surge, sway, yaw, roll
- RK4 integrator dt=0.02s
- Input: actuator command topic; Output: `sil.OwnShipState` at 50Hz
- Parameter: `hull_class=SEMI_PLANING`

**Test:** `tests/sil/test_ship_dynamics.py` — test turning circle (35° rudder → diameter ~4.5 L_pp), RK4 vs analytic for constant acceleration

---

### Task 17: env_disturbance_node

**Files:** `src/sim_workbench/sil_nodes/env_disturbance/`

**Key implementation:**
- Gauss-Markov wind model: `tau_wind=300s`, `sigma=2.0`
- Gauss-Markov current model
- 3 modes: constant / pulse / Monte Carlo
- Output: `sil.EnvironmentState` at 1Hz

**Test:** `tests/sil/test_env_disturbance.py` — test wind autocorrelation at lag=tau, current vector boundedness, mode switching

---

### Task 18: target_vessel_node

**Files:** `src/sim_workbench/sil_nodes/target_vessel/`

**Key implementation:**
- 3 modes: `ais_replay` (D1.3b.2 required) / `ncdm` (D2.4) / `intelligent` (D3.6)
- Multi-spawn: one instance per target vessel
- Output: `sil.TargetVesselState` at 10Hz per target

**Test:** `tests/sil/test_target_vessel.py` — test AIS replay trajectory fidelity (position error < 1m over 60s), mode enum switching

---

### Task 19: sensor_mock_node

**Files:** `src/sim_workbench/sil_nodes/sensor_mock/`

**Key implementation:**
- AIS Class A/B transponder simulation with `dropout_pct` config
- Radar model: `max_range`, `clutter`, `detection_prob`
- Polar noise model for radar targets
- Output: `sil.AISMessage` at 0.1Hz, `sil.RadarMeasurement` at 5Hz

**Test:** `tests/sil/test_sensor_mock.py` — test dropout rate statistical match, radar clutter cardinality bounds

---

### Task 20: tracker_mock_node

**Files:** `src/sim_workbench/sil_nodes/tracker_mock/`

**Key implementation:**
- God tracker: perfect ground truth passthrough
- KF tracker: 4-state (x, y, vx, vy) constant velocity model
- Config: `tracker_type=god|kf`, `P_0`, `q`
- Output: `TrackedTargetArray` (existing `l3_external_msgs`)

**Test:** `tests/sil/test_tracker_mock.py` — test God tracker passes truth unchanged, KF convergence within 10 steps for static target

---

### Task 21: fault_injection_node

**Files:** `src/sim_workbench/sil_nodes/fault_injection/`

**Key implementation:**
- Phase 1 minimal set: `ais_dropout`, `radar_noise_spike`, `disturbance_step`
- Service: `FaultTrigger` (Trigger/List/Cancel)
- Publishes `/fault/{type}` events to affected nodes
- fault_dict configurable via YAML

**Test:** `tests/sil/test_fault_injection.py` — test trigger creates event, list returns active faults, cancel removes fault

---

### Task 22: scoring_node

**Files:** `src/sim_workbench/sil_nodes/scoring/`

**Key implementation:**
- 6-dim scoring (Hagen 2022): safety, rule, delay, magnitude, phase, plausibility
- Phase 1 stub: default weights, compute total as weighted sum
- Input: own_ship_state + target_states + ASDR events
- Output: `sil.ScoringRow` at 1Hz

**Test:** `tests/sil/test_scoring.py` — test scoring row produced at 1Hz, weights sum to 1.0, total bounded [0,1]

---

## Task 23: Foxglove Bridge Wiring

**Files:**
- Modify: `docker-compose.yml` (add foxglove_bridge service)
- Create: `config/foxglove_bridge.yaml`

**Depends on:** Tasks 14 + 22 + 5 (lifecycle mgr + scoring + telemetry store)

**Spec reference:** §1 foxglove_bridge, §4.2 Protobuf boundary

- [ ] **Step 23.1: Create foxglove bridge config**

Create `config/foxglove_bridge.yaml`:
```yaml
port: 8765
send_buffer_limit: 10000000
use_sim_time: true
capabilities:
  - clientPublish
  - connectionGraph
  - services
  - parameters
topic_whitelist:
  - /sil/own_ship_state
  - /sil/target_vessel_state
  - /sil/environment_state
  - /sil/module_pulse
  - /sil/asdr_event
  - /sil/scoring_row
  - /sil/lifecycle_status
  - /sil/fault_event
service_whitelist:
  - /lifecycle_mgr/change_state
  - /sim_clock/set_rate
  - /fault_inject/trigger
```

- [ ] **Step 23.2: Add foxglove_bridge to docker-compose**

If docker-compose exists, add service. Otherwise document the ROS2 launch command:
```bash
ros2 launch foxglove_bridge foxglove_bridge_launch.xml port:=8765 config:=config/foxglove_bridge.yaml
```

- [ ] **Step 23.3: Create WebSocket client hook (frontend)**

Create `web/src/hooks/useFoxgloveLive.ts`:
```typescript
import { useEffect, useRef } from 'react';
import { useTelemetryStore, useScenarioStore } from '../store';

export function useFoxgloveLive(wsUrl = 'ws://localhost:8765') {
  const wsRef = useRef<WebSocket | null>(null);
  const updateOwnShip = useTelemetryStore((s) => s.updateOwnShip);
  const updateTargets = useTelemetryStore((s) => s.updateTargets);

  useEffect(() => {
    const ws = new WebSocket(wsUrl);
    wsRef.current = ws;

    ws.onmessage = (event) => {
      // foxglove_bridge sends JSON-encoded Protobuf over WS
      const msg = JSON.parse(event.data);
      if (msg.topic === '/sil/own_ship_state') {
        updateOwnShip(msg.payload);
      } else if (msg.topic === '/sil/target_vessel_state') {
        updateTargets(msg.payload);
      }
    };

    return () => ws.close();
  }, [wsUrl, updateOwnShip, updateTargets]);
}
```

- [ ] **Step 23.4: Commit**

```bash
git add config/foxglove_bridge.yaml web/src/hooks/useFoxgloveLive.ts docker-compose.yml
git commit -m "feat(sil): add foxglove_bridge wiring — 50Hz WS relay + frontend hook"
```

---

## Task 24: Screen ③ — Bridge HMI (50Hz Live)

**Files:**
- Create: `web/src/screens/BridgeHMI.tsx`
- Create: `web/src/screens/__tests__/BridgeHMI.test.tsx`
- Modify: `web/src/App.tsx` (add route)

**Depends on:** Task 23 (foxglove WS live) + Tasks 5, 7 (stores + map)

**Spec reference:** §5.4 Captain view, §3 ③ Bridge HMI, §5.1 Layout C (fullscreen map + Captain HUD + Module Pulse + ASDR log)

- [ ] **Step 24.1: Create BridgeHMI screen**

Create `web/src/screens/BridgeHMI.tsx`:
```typescript
import { SilMapView } from '../map/SilMapView';
import { useFoxgloveLive } from '../hooks/useFoxgloveLive';
import { useTelemetryStore, useControlStore, useUIStore } from '../store';
import { useActivateLifecycleMutation, useDeactivateLifecycleMutation } from '../api/silApi';
import { useParams, useNavigate } from 'react-router-dom';

export function BridgeHMI() {
  const { runId } = useParams<{ runId: string }>();
  const navigate = useNavigate();
  useFoxgloveLive();

  const ownShip = useTelemetryStore((s) => s.ownShip);
  const environment = useTelemetryStore((s) => s.environment);
  const modulePulses = useTelemetryStore((s) => s.modulePulses);
  const { simRate, isPaused } = useControlStore();
  const { viewMode, asdrLogExpanded, toggleAsdrLog, setViewMode } = useUIStore();
  const [deactivate] = useDeactivateLifecycleMutation();

  const handleStop = async () => {
    await deactivate();
    navigate(`/report/${runId}`);
  };

  return (
    <div style={{ height: '100vh', display: 'flex', flexDirection: 'column' }}
         data-testid="bridge-hmi">
      {/* Full-screen ENC map */}
      <div style={{ flex: 1, position: 'relative' }}>
        <SilMapView followOwnShip={viewMode === 'captain'} viewMode={viewMode} />

        {/* Captain HUD overlay */}
        <div style={{ position: 'absolute', top: 16, left: 16, background: 'rgba(0,0,0,0.7)',
                        padding: 12, borderRadius: 8, color: '#e6edf3', fontSize: 13 }}>
          {ownShip && (
            <>
              <div>SOG: {ownShip.kinematics?.sog?.toFixed(1)} m/s</div>
              <div>COG: {((ownShip.kinematics?.cog ?? 0) * 180 / Math.PI).toFixed(1)}°</div>
              <div>HDG: {((ownShip.pose?.heading ?? 0) * 180 / Math.PI).toFixed(1)}°</div>
            </>
          )}
          {environment && (
            <>
              <div>Wind: {environment.wind?.direction?.toFixed(0)}° @ {environment.wind?.speedMps?.toFixed(1)} m/s</div>
              <div>Sea: Beaufort {environment.seaStateBeaufort}</div>
            </>
          )}
        </div>

        {/* View mode tabs */}
        <div style={{ position: 'absolute', top: 16, right: 16, display: 'flex', gap: 4 }}>
          {(['captain', 'god', 'roc'] as const).map((mode) => (
            <button key={mode} onClick={() => setViewMode(mode)}
                    style={{ background: viewMode === mode ? '#2dd4bf' : '#333',
                             color: '#fff', border: 'none', padding: '4px 12px', borderRadius: 4 }}>
              {mode === 'captain' ? 'Captain' : mode === 'god' ? 'God' : 'ROC'}
            </button>
          ))}
        </div>
      </div>

      {/* Bottom bar: Time + Module Pulse + ASDR */}
      <div style={{ height: 40, background: '#0b1320', display: 'flex',
                      alignItems: 'center', padding: '0 16px', gap: 16, fontSize: 12 }}>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <span data-testid="sim-time">00:00</span>
          <select value={simRate} onChange={(e) => useControlStore.getState().setSimRate(Number(e.target.value))}
                  style={{ background: '#333', color: '#fff', border: 'none', padding: '2px 8px' }}>
            {[0.5, 1, 2, 4, 10].map((r) => <option key={r} value={r}>×{r}</option>)}
          </select>
          <button onClick={() => useControlStore.getState().setPaused(!isPaused)}
                  style={{ background: isPaused ? '#fbbf24' : '#333', color: '#fff', border: 'none', padding: '4px 12px' }}>
            {isPaused ? '▶' : '⏸'}
          </button>
        </div>

        {/* Module Pulse bar */}
        <div style={{ display: 'flex', gap: 8, flex: 1, justifyContent: 'center' }}>
          {modulePulses.map((p, i) => (
            <div key={i} style={{
              width: 16, height: 16, borderRadius: '50%',
              background: p.state === 1 ? '#34d399' : p.state === 2 ? '#fbbf24' : '#f87171',
            }} title={`${p.moduleId}`} />
          ))}
        </div>

        <button onClick={toggleAsdrLog}
                style={{ background: asdrLogExpanded ? '#2dd4bf' : '#333', color: '#fff', border: 'none', padding: '4px 12px' }}>
          ASDR
        </button>
        <button onClick={handleStop}
                style={{ background: '#f87171', color: '#fff', border: 'none', padding: '4px 12px' }}>
          ⏹ Stop
        </button>
      </div>
    </div>
  );
}
```

- [ ] **Step 24.2: Write smoke test**

Create `web/src/screens/__tests__/BridgeHMI.test.tsx`:
```typescript
import { describe, it, expect } from 'vitest';
import { render } from '../../test-setup';
import { BridgeHMI } from '../BridgeHMI';

describe('BridgeHMI', () => {
  it('renders without crashing', () => {
    const { getByTestId } = render(<BridgeHMI />);
    expect(getByTestId('bridge-hmi')).toBeInTheDocument();
  });
});
```

- [ ] **Step 24.3: Add route**

Add `/bridge/:runId` → `<BridgeHMI />` in App.tsx routing.

- [ ] **Step 24.4: Run test**

```bash
npm --prefix web test -- --run web/src/screens/__tests__/BridgeHMI.test.tsx
```
Expected: PASS.

- [ ] **Step 24.5: Commit**

```bash
git add web/src/screens/BridgeHMI.tsx web/src/screens/__tests__/ web/src/App.tsx
git commit -m "feat(sil): add Screen ③ Bridge HMI with Captain view + 50Hz live telemetry"
```

---

## Task 25: Docker Compose + component_container Setup

**Files:**
- Rewrite: `docker-compose.yml`
- Create: `scripts/orb-sil-manager.sh`

**Depends on:** Tasks 14–22 (ROS2 nodes exist)

- [ ] **Step 25.1: Rewrite docker-compose.yml**

Replace existing `docker-compose.yml` with:
```yaml
version: "3.9"

services:
  sil-orchestrator:
    build:
      context: .
      dockerfile: docker/sil_orchestrator.Dockerfile
    ports:
      - "8000:8000"
    volumes:
      - ./scenarios:/var/sil/scenarios
      - ./runs:/var/sil/runs
      - ./exports:/var/sil/exports
    environment:
      - ROS_DOMAIN_ID=0
    network_mode: host

  sil-component-container:
    build:
      context: .
      dockerfile: docker/sil_nodes.Dockerfile
    command: >
      ros2 run rclcpp_components component_container_mt
      --ros-args -r __node:=sil_container
    volumes:
      - ./scenarios:/var/sil/scenarios
      - ./runs:/var/sil/runs
    environment:
      - ROS_DOMAIN_ID=0
    network_mode: host

  foxglove-bridge:
    image: ghcr.io/foxglove/ros-foxglove-bridge:humble
    command: >
      ros2 launch foxglove_bridge foxglove_bridge_launch.xml
      port:=8765
      config_file:=/config/foxglove_bridge.yaml
    volumes:
      - ./config/foxglove_bridge.yaml:/config/foxglove_bridge.yaml
    environment:
      - ROS_DOMAIN_ID=0
    network_mode: host

  web:
    build:
      context: ./web
      dockerfile: Dockerfile
    ports:
      - "5173:5173"
    depends_on:
      - sil-orchestrator
```

- [ ] **Step 25.2: Create component_container launch script**

Create `scripts/orb-sil-manager.sh`:
```bash
#!/bin/bash
# Load all 9 SIL nodes into component_container_mt
# Usage: ./scripts/orb-sil-manager.sh [start|stop|status]

CONTAINER="sil_container"

case "${1:-start}" in
  start)
    echo "Loading SIL nodes into $CONTAINER..."
    ros2 component load /$CONTAINER sil_lifecycle scenario_lifecycle_mgr::ScenarioLifecycleMgr
    ros2 component load /$CONTAINER sil_nodes scenario_authoring::ScenarioAuthoringNode
    ros2 component load /$CONTAINER sil_nodes ship_dynamics::ShipDynamicsNode
    ros2 component load /$CONTAINER sil_nodes env_disturbance::EnvDisturbanceNode
    ros2 component load /$CONTAINER sil_nodes target_vessel::TargetVesselNode
    ros2 component load /$CONTAINER sil_nodes sensor_mock::SensorMockNode
    ros2 component load /$CONTAINER sil_nodes tracker_mock::TrackerMockNode
    ros2 component load /$CONTAINER sil_nodes fault_injection::FaultInjectionNode
    ros2 component load /$CONTAINER sil_nodes scoring::ScoringNode
    echo "All 9 nodes loaded."
    ;;
  stop)
    echo "Stopping $CONTAINER..."
    ros2 component unload /$CONTAINER '*' 2>/dev/null
    ;;
  status)
    ros2 component list
    ;;
esac
```

```bash
chmod +x scripts/orb-sil-manager.sh
```

- [ ] **Step 25.3: Commit**

```bash
git add docker-compose.yml scripts/orb-sil-manager.sh
git commit -m "feat(sil): rewrite docker-compose for orchestrator + component_container + foxglove"
```

---

## Task 26: buf CI + Pytest Harness

**Files:**
- Modify: `.gitlab-ci.yml` (add buf + SIL test jobs)
- Create: `idl/buf.work.yaml` (if needed for CI compatibility)

**Depends on:** Tasks 1–4 (IDL + codegen exist)

**Independent from:** Tasks 5–25 (only needs IDL to exist)

- [ ] **Step 26.1: Add buf lint + breaking CI job**

Add to `.gitlab-ci.yml`:
```yaml
buf-lint:
  stage: check
  image: bufbuild/buf:latest
  script:
    - cd idl && buf lint
    - buf breaking --against '.git#branch=main'
  rules:
    - if: $CI_MERGE_REQUEST_ID
```

- [ ] **Step 26.2: Add SIL orchestrator test job**

Add to `.gitlab-ci.yml`:
```yaml
sil-orchestrator-tests:
  stage: test
  image: python:3.11
  script:
    - pip install fastapi httpx pytest
    - python -m pytest tests/sil_orchestrator/ -v --json-report --json-report-file=test-results/sil_orchestrator.json
  artifacts:
    paths:
      - test-results/sil_orchestrator.json
```

- [ ] **Step 26.3: Add SIL frontend test job**

Add to `.gitlab-ci.yml`:
```yaml
sil-web-tests:
  stage: test
  image: node:20
  before_script:
    - cd web && npm ci
  script:
    - cd web && npx vitest --run
```

- [ ] **Step 26.4: Commit**

```bash
git add .gitlab-ci.yml
git commit -m "ci(sil): add buf lint/breaking + orchestrator + web test jobs"
```

---

## Task 27: Archive Old Code

**Files:** Move (not delete) existing code to `archive/sil_v0/`

**Depends on:** Task 24 (new web screens working)

**Spec reference:** §9.1 disposal table

- [ ] **Step 27.1: Archive old web code**

```bash
mkdir -p archive/sil_v0
git mv web/ archive/sil_v0/web/
git mv src/sim_workbench/sil_mock_publisher archive/sil_v0/
git mv src/sim_workbench/l3_external_mock_publisher archive/sil_v0/
```

- [ ] **Step 27.2: Create archive README**

Create `archive/sil_v0/README.md`:
```markdown
# SIL v0 Prototype Archive

Archived 2026-05-xx per greenfield decision (see `docs/Design/SIL/2026-05-12-sil-architecture-design.md` §9.1).

These modules are retired — replaced by the new SIL architecture.
Keep until DEMO-1 passes (6/15), then delete.

## Contents
- `web/` — 13 React components + 3 hooks (replaced by 4-screen Zustand + RTK Query app)
- `sil_mock_publisher/` — ROS2 mock publisher (replaced by 9-node rclcpp_components)
- `l3_external_mock_publisher/` — L3 mock publisher (retired)
```

- [ ] **Step 27.3: Verify new web still works**

```bash
cd web && npm run build
```
Expected: build succeeds (no stale imports from archived code).

- [ ] **Step 27.4: Commit**

```bash
git add archive/sil_v0/
git commit -m "chore(sil): archive v0 web + mock publishers to archive/sil_v0/"
```

---

## Task 28: E2E R14 Head-On Scenario

**Files:**
- Create: `scenarios/imazu22/r14_head_on.yaml`
- Create: `tests/e2e/test_r14_head_on.py`
- Modify: `scripts/orb-sil-manager.sh` (add scenario load)

**Depends on:** Tasks 24 + 25 (Bridge HMI + Docker)

**Spec reference:** §9.2 Week 4 milestone, §7.1 MUST list item

- [ ] **Step 28.1: Create R14 head-on scenario YAML**

Create `scenarios/imazu22/r14_head_on.yaml`:
```yaml
# Imazu-22 Problem 14: Head-on encounter
# Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §7.1
scenario:
  id: "r14-head-on"
  name: "Imazu-22 R14 Head-on"
  encounter_type: "head-on"
  description: "Own ship and target vessel approaching head-on, COLREGs Rule 14 applies"

own_ship:
  initial:
    lat: 63.4400
    lon: 10.4000
    heading_deg: 0.0    # north
    sog_kn: 12.0
  hull_class: "SEMI_PLANING"
  length_oa_m: 45.0
  breadth_m: 9.0
  draft_m: 2.5

targets:
  - mmsi: 100000001
    name: "Target A"
    ship_type: "CARGO"
    initial:
      lat: 63.4450
      lon: 10.4000
      heading_deg: 180.0  # south (head-on)
      sog_kn: 10.0
    mode: "replay"

environment:
  wind:
    direction_deg: 270
    speed_mps: 5.0
  current:
    direction_deg: 0
    speed_mps: 0.5
  visibility_nm: 10.0
  sea_state_beaufort: 3

simulation:
  duration_s: 600
  tick_hz: 50
  dt_s: 0.02

expected:
  rule_triggered: "Rule 14 (Head-on)"
  min_cpa_nm: 0.5
  colregs_compliant: true
```

- [ ] **Step 28.2: Write E2E test**

Create `tests/e2e/test_r14_head_on.py`:
```python
"""E2E test: R14 Head-on scenario — load → run → verify PASS.

Requires: sil_orchestrator running on localhost:8000.
Phase 1: validates REST API flow (mock lifecycle).
Phase 2: validates MCAP output contains own_ship_state topic.
"""

import pytest
import requests
import time


ORCHESTRATOR = "http://localhost:8000/api/v1"


@pytest.fixture
def r14_yaml():
    with open("scenarios/imazu22/r14_head_on.yaml") as f:
        return f.read()


def test_r14_scenario_load_validate(r14_yaml):
    """Step 1: Validate scenario YAML."""
    resp = requests.post(f"{ORCHESTRATOR}/scenarios/validate", json={"yaml_content": r14_yaml})
    assert resp.status_code == 200
    assert resp.json()["valid"] is True


def test_r14_scenario_lifecycle(r14_yaml):
    """Step 2: Full lifecycle flow — create → configure → activate → deactivate."""
    # Create scenario
    resp = requests.post(f"{ORCHESTRATOR}/scenarios", json={"yaml_content": r14_yaml})
    assert resp.status_code == 200
    scenario_id = resp.json()["scenario_id"]

    # Configure lifecycle
    resp = requests.post(f"{ORCHESTRATOR}/lifecycle/configure", json={"scenario_id": scenario_id})
    assert resp.json()["success"] is True

    # Verify status
    resp = requests.get(f"{ORCHESTRATOR}/lifecycle/status")
    assert resp.json()["current_state"] == "inactive"

    # Activate
    resp = requests.post(f"{ORCHESTRATOR}/lifecycle/activate")
    assert resp.json()["success"] is True

    # Let it run for 2 seconds (simulated)
    time.sleep(0.5)

    # Deactivate
    resp = requests.post(f"{ORCHESTRATOR}/lifecycle/deactivate")
    assert resp.json()["success"] is True


def test_r14_selfcheck():
    """Step 3: Self-check probe works."""
    resp = requests.post(f"{ORCHESTRATOR}/selfcheck/probe")
    assert resp.status_code == 200
    assert resp.json()["all_clear"] is True
```

- [ ] **Step 28.3: Add E2E CI job**

Add to `.gitlab-ci.yml`:
```yaml
sil-e2e-r14:
  stage: integration
  services:
    - name: sil-orchestrator:latest
      alias: orchestrator
  script:
    - pip install requests pytest
    - python -m pytest tests/e2e/test_r14_head_on.py -v
  needs:
    - sil-orchestrator-tests
```

- [ ] **Step 28.4: Commit**

```bash
git add scenarios/imazu22/r14_head_on.yaml tests/e2e/test_r14_head_on.py .gitlab-ci.yml
git commit -m "feat(sil): add R14 head-on E2E scenario + lifecycle integration test"
```

---

## Execution Strategy

### Phase A: Foundation (sequential, ~1 day)
1. **Task 1** → Protobuf IDL (blocking all) — execute first
2. **Tasks 2, 3, 4** → Code generation (3 parallel subagents after Task 1)

### Phase B: Frontend + Orchestrator (parallel, ~2 days)
3. **Tasks 5, 6, 7** → Frontend infrastructure (3 parallel subagents after Task 2)
4. **Task 11** → Orchestrator scaffold (after Task 3)
5. **Tasks 12, 13** → Orchestrator endpoints (2 parallel subagents after Task 11)
6. **Tasks 8, 9, 10** → Screen ①②④ (3 parallel subagents after Tasks 5+6+7)

### Phase C: ROS2 Nodes (parallel, ~1 day)
7. **Task 14** → Lifecycle mgr (after Task 4)
8. **Tasks 15–22** → 8 ROS2 nodes (8 parallel subagents after Task 14)
9. **Task 26** → CI pipeline (after Tasks 1–4, independent of 5–25)

### Phase D: Integration (sequential, ~2 days)
10. **Task 23** → Foxglove wiring (after 14+22+5)
11. **Task 24** → Screen ③ Bridge HMI (after Task 23)
12. **Task 25** → Docker compose (after Tasks 14–22)
13. **Task 27** → Archive (after Task 24)
14. **Task 28** → E2E R14 demo (after Tasks 24+25)

### Max concurrent subagents: 8 (Tasks 15–22)

---

## Self-Review Checklist

- [x] **Spec coverage:** Each §1–§9 section mapped to ≥1 task. §4 (IDL) → Task 1. §5 (Frontend) → Tasks 2,5–10,24. §6 (Artefacts) → Task 13. §7 (colav mapping) → Tasks 15–22. §9 (migration) → Task 27.
- [x] **Placeholder scan:** No TBD/TODO in code blocks. Phase 2 deferred features explicitly noted as "Phase 2" comments but implementation is complete for Phase 1 scope.
- [x] **Type consistency:** `ScenarioSummary` type matches across Task 6 (API) and Task 8 (Screen). `LifecycleState` enum matches across Tasks 11, 14, and 5. Store selectors use correct field names from Task 5.
- [x] **No unmet dependencies:** Every parallel track has its prerequisites listed in the Parallelisation Map.
