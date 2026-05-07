"""
web_server.py — Flask web server for the ROC HMI Simulator dashboard.

Routes:
  GET  /                  Serve dashboard.html
  GET  /api/state         Return current node state as JSON
  POST /api/tor_response  Accept {"response": "ACCEPT"|"REJECT"|"DEFER"}
                          and forward to the ROS2 node

This module is imported by launch_roc.py, which starts it in a daemon
thread in the same process as the ROS2 node.  It can also be run standalone
for UI development (state will be empty/None).
"""

import json
import os
import time
from pathlib import Path

from flask import Flask, jsonify, render_template, request

# roc_node is in the same process; get_node() returns the live singleton.
import roc_node as _roc_node_module

app = Flask(
    __name__,
    template_folder=str(Path(__file__).parent / "templates"),
    static_folder=str(Path(__file__).parent / "static"),
)

# Port can be overridden via env var or programmatically before serve()
PORT: int = int(os.environ.get("ROC_WEB_PORT", 8765))


@app.route("/")
def index():
    return render_template("dashboard.html")


@app.route("/api/state")
def api_state():
    """Return the latest ROC state as JSON (polled every 1 s by the dashboard)."""
    node = _roc_node_module.get_node()
    if node is None:
        # Node not yet started — return empty shell so the UI renders gracefully
        state = {
            "ui_state": None,
            "sat_data": None,
            "tor_request": None,
            "tor_received_at": None,
            "tor_response": None,
        }
    else:
        state = node.get_state()

    # Add server timestamp so the UI can detect stale data
    state["server_time"] = time.time()
    return jsonify(state)


@app.route("/api/tor_response", methods=["POST"])
def api_tor_response():
    """Accept a JSON body {\"response\": \"ACCEPT\"|\"REJECT\"|\"DEFER\"} and forward."""
    body = request.get_json(silent=True)
    if not body or "response" not in body:
        return jsonify({"ok": False, "error": "Missing 'response' field"}), 400

    response_str = str(body["response"]).upper()
    if response_str not in ("ACCEPT", "REJECT", "DEFER"):
        return jsonify({"ok": False, "error": f"Invalid response: {response_str!r}"}), 400

    node = _roc_node_module.get_node()
    if node is None:
        return jsonify({"ok": False, "error": "ROS2 node not yet initialised"}), 503

    published = node.respond_to_tor(response_str)
    if not published:
        return jsonify({"ok": False, "error": "No pending ToR request"}), 409

    return jsonify({"ok": True, "response": response_str})


def serve(port: int = PORT, debug: bool = False) -> None:
    """Start Flask development server (blocking)."""
    app.run(host="0.0.0.0", port=port, debug=debug, use_reloader=False)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="ROC HMI web server (standalone)")
    parser.add_argument("--port", type=int, default=PORT, help="HTTP port (default 8765)")
    parser.add_argument("--debug", action="store_true", help="Flask debug mode")
    args = parser.parse_args()
    serve(port=args.port, debug=args.debug)
