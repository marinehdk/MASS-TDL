"""
launch_roc.py — Combined launcher for the ROC HMI Simulator.

Starts the Flask web server in a daemon thread (same process as the ROS2
node), then blocks on rclpy.spin in the main thread.  On Ctrl-C the daemon
thread exits automatically when the main thread exits.

Usage:
  python tools/roc_simulator/launch_roc.py [--auto-respond] [--port 8765]
"""

import argparse
import signal
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


def start_web_server_thread(port: int) -> threading.Thread:
    """Start the Flask web server in a daemon thread (same process as the ROS2 node)."""
    import web_server as _web_server_module

    t = threading.Thread(
        target=_web_server_module.serve,
        kwargs={"port": port, "debug": False},
        daemon=True,
        name="roc-flask",
    )
    t.start()
    return t


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

    # ── 1. Initialise roc_node singleton (must happen before Flask starts
    #        so that get_node() is non-None when the first HTTP request arrives)
    import roc_node

    # ── 2. Start Flask web server in a daemon thread ─────────────────
    print(f"[launch_roc] Starting web server on port {args.port} …")
    start_web_server_thread(port=args.port)

    if wait_for_server(args.port):
        print(f"[launch_roc] Dashboard available at http://localhost:{args.port}")
    else:
        print(
            f"[launch_roc] Warning: web server did not respond within 10 s "
            f"(port {args.port}). Continuing anyway."
        )

    # ── 3. Register shutdown handler ────────────────────────────────
    def shutdown(signum=None, frame=None):
        print("\n[launch_roc] Shutting down …")
        # Daemon thread exits automatically; just stop the ROS2 runtime.
        try:
            import rclpy
            rclpy.shutdown()
        except Exception:
            pass
        sys.exit(0)

    signal.signal(signal.SIGINT,  shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # ── 4. Start ROS2 node (blocks on rclpy.spin in main thread) ────
    print(
        f"[launch_roc] Starting ROS2 node "
        f"(auto_respond={'on' if args.auto_respond else 'off'}) …"
    )
    try:
        roc_node.start_node(auto_response_mode=args.auto_respond)
    except ImportError as exc:
        print(f"[launch_roc] ERROR: could not import roc_node: {exc}")
        print("[launch_roc] Is rclpy available in this environment?")
        # Keep the web server thread alive so the UI can still be inspected
        print("[launch_roc] Web server remains running. Press Ctrl-C to stop.")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            shutdown()
    except KeyboardInterrupt:
        shutdown()

    # rclpy.spin() returned normally
    shutdown()


if __name__ == "__main__":
    main()
