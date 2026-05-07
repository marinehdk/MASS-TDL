# ROC HMI Simulator

Remote Operations Center operator interface for MASS L3 Tactical Layer HIL tests.

Subscribes to M8 (`m8_hmi_transparency_bridge`) transparency outputs and provides:
- A lightweight web dashboard for monitoring ship state and SAT-1/2/3 data
- Interactive ToR (Transfer of Responsibility) response buttons
- An `--auto-respond` mode that accepts ToR automatically (for automated HIL runs)

---

## Quick start

```bash
# Normal mode — operator responds to ToR manually via the browser
python tools/roc_simulator/launch_roc.py

# Automated HIL mode — ToR auto-ACCEPTed after 5 seconds
python tools/roc_simulator/launch_roc.py --auto-respond

# Custom port
python tools/roc_simulator/launch_roc.py --port 9000
```

Open the dashboard in a browser: **http://localhost:8765**

---

## `--auto-respond` flag

When `--auto-respond` is set the ROS2 node will automatically publish `ACCEPT`
to `/l3/roc/tor_response` 5 seconds after each incoming `ToRRequest` message,
**unless the operator has already responded manually**.

This is intended for automated HIL test scenarios (e.g. pytest-ros integration
tests) that need to verify the full ToR handshake without a human in the loop.

---

## Topics

| Direction | Topic | Message type | Description |
|-----------|-------|--------------|-------------|
| Subscribe | `/l3/m8/ui_state` | `l3_msgs/UIState` | Live ship state @ 50 Hz |
| Subscribe | `/l3/m8/tor_request` | `l3_msgs/ToRRequest` | ToR handshake request |
| Subscribe | `/l3/m8/sat_data` | `l3_msgs/SATData` | SAT-1/2/3 transparency data |
| Publish | `/l3/roc/tor_response` | `std_msgs/String` | Operator response: `ACCEPT` / `REJECT` / `DEFER` |

---

## File structure

```
tools/roc_simulator/
  roc_node.py        ROS2 Python node (subscriptions + ToR publisher)
  web_server.py      Flask web server (dashboard API + static files)
  templates/
    dashboard.html   Operator dashboard (auto-refreshes every 1 s)
  static/
    style.css        Dashboard styles
  launch_roc.py      Combined launcher (web server subprocess + ROS2 node)
  README.md          This file
```

---

## Running components separately

```bash
# ROS2 node only
python tools/roc_simulator/roc_node.py [--auto-respond]

# Web server only (no ROS2 required — useful for UI development)
python tools/roc_simulator/web_server.py [--port 8765] [--debug]
```

---

## Dependencies

- `rclpy` (ROS2 Jazzy Python client library)
- `flask` (web server)
- `l3_msgs` (custom message package — must be built and sourced)
- Python standard library only for everything else

No additional pip dependencies beyond the HIL environment baseline.
