"""
launch_roc.py — Combined launcher for the ROC HMI Simulator.

Starts the Flask web server in a subprocess, then runs the ROS2 node in the
main process (blocking on rclpy.spin).  On Ctrl-C both are shut down cleanly.

Usage:
  python tools/roc_simulator/launch_roc.py [--auto-respond] [--port 8765]
"""

import argparse
import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

# Ensure the simulator package is importable regardless of cwd
_HERE = Path(__file__).resolve().parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="ROC HMI Simulator launcher")
    parser.add_argument(
        "--auto-respond",
        action="store_true",
        help="Auto-ACCEPT any ToR after 5 s (for automated HIL runs)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=8765,
        help="HTTP port for the dashboard web server (default: 8765)",
    )
    return parser.parse_args()


def start_web_server(port: int) -> subprocess.Popen:
    """Start web_server.py in a child process."""
    env = os.environ.copy()
    env["ROC_WEB_PORT"] = str(port)
    proc = subprocess.Popen(
        [sys.executable, str(_HERE / "web_server.py"), "--port", str(port)],
        env=env,
    )
    return proc


def wait_for_server(port: int, timeout: float = 10.0) -> bool:
    """Poll until the Flask server accepts connections or timeout."""
    import socket

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.2)
    return False


def main() -> None:
    args = parse_args()

    # ── 1. Start Flask web server subprocess ────────────────────────
    print(f"[launch_roc] Starting web server on port {args.port} …")
    web_proc = start_web_server(args.port)

    if wait_for_server(args.port):
        print(f"[launch_roc] Dashboard available at http://localhost:{args.port}")
    else:
        print(
            f"[launch_roc] Warning: web server did not respond within 10 s "
            f"(port {args.port}). Continuing anyway."
        )

    # ── 2. Register shutdown handler ────────────────────────────────
    def shutdown(signum=None, frame=None):
        print("\n[launch_roc] Shutting down …")
        web_proc.terminate()
        try:
            web_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            web_proc.kill()
        sys.exit(0)

    signal.signal(signal.SIGINT,  shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # ── 3. Start ROS2 node (blocks until shutdown) ───────────────────
    print(
        f"[launch_roc] Starting ROS2 node "
        f"(auto_respond={'on' if args.auto_respond else 'off'}) …"
    )
    try:
        import roc_node
        roc_node.start_node(auto_response_mode=args.auto_respond)
    except ImportError as exc:
        print(f"[launch_roc] ERROR: could not import roc_node: {exc}")
        print("[launch_roc] Is rclpy available in this environment?")
        # Keep the web server alive so the UI can still be inspected
        print("[launch_roc] Web server remains running. Press Ctrl-C to stop.")
        try:
            web_proc.wait()
        except KeyboardInterrupt:
            shutdown()
    except KeyboardInterrupt:
        shutdown()

    # rclpy.spin() returned normally
    shutdown()


if __name__ == "__main__":
    main()
