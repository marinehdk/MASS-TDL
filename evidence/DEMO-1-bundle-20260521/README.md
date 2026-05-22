# DEMO-1 Showcase Bundle (CCS Type Approval Evidence Pack)

This Showcase Bundle contains all mandatory E2E engineering and safety validation artifacts required by **maritime-v3.0 §10** for the **DEMO-1 R14 Head-On Encounter** scenario.

---

## Showcase Artifacts Index (5 Required Charter Items)

### 1. E2E Video Recording (≤ 5 min)
*   **File**: `demo1-20260521-143547.mp4` (QuickTime native H.264, 1280x720 @ 30fps)
*   **Subtitles**: Features embedded, soft-subtitle annotations highlighting 5 key moments:
    1. **0s**: Scenario start (Head-on encounter).
    2. **60s**: Encounter active, Rule 14 determined.
    3. **120s**: Starboard maneuver execution (+35 deg rudder).
    4. **240s**: Safe CPA clearance and route recovery.
    5. **280s**: Scenario completion with zero safety violations.

### 2. Scenario Specification YAML File
*   **File**: `imazu-08.yaml`
*   **Description**: Fully compliant maritime-schema v3.0 scenario specifying the initial encounter conditions, own ship state, target ship paths, and environmental criteria.

### 3. ASDR (Autonomous Safety & Decision Registry) Decision Log
*   **File**: `demo1-asdr-20260521-143547.jsonl`
*   **Description**: Real-time structured telemetry capturing own_ship, target states, module heartbeats, and ASDR decision logs. Fully compliance audit-trail demonstrating continuous 10Hz safety registry records.

### 4. PDF presentation documents (Professional High-Fidelity)
*   **ConOps**: `ConOps-v0.1.pdf` (Concept of Operations explaining tactical layer design)
*   **V&V Plan**: `VV-Plan-v0.1.pdf` (Verification & Validation Strategy)
*   **Simulator Qualification**: `simulator-qualification-report.pdf` (MMG dynamics fidelity analysis)
*   **Evidence Tracking Matrix**: `cert-evidence-tracking.pdf` (Traceability checklist for CCS Type Approval)

### 5. "What this means for TDL" PM Narrative
*   **File**: `PM-Showcase-Narrative.md`
*   **Description**: Narrative explaining the engineering and regulatory value of closing the Y-axis Reflex Arc避碰 control loop and implementing the Z-axis hardware override safety cutoff.

---

## Timed Frames & Screenshots
*   **Directory**: `frames/`
*   **Keyframes**: High-res screenshots captured during active execution:
    *   `frame-10s.png`, `frame-30s.png`, `frame-60s.png`, `frame-120s.png`, `frame-180s.png`, `frame-240s.png`, `frame-270s.png`.

## ROS 2 bag Archive
*   **Directory**: `demo1-rosbag-20260521-143547/`
*   **Description**: Contains native ROS 2 Humble bag recordings of all topics under `/sil/*` and `/l3/*` namespaces, allowing complete replay fidelity.
