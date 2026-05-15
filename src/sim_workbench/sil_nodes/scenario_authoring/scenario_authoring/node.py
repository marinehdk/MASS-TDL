"""Scenario Authoring Node — YAML CRUD, Imazu-22, AIS pipeline."""
import hashlib
import uuid
from pathlib import Path

import rclpy
from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn
from std_msgs.msg import String


class ScenarioAuthoringNode(LifecycleNode):
    """Filesystem-backed YAML scenario CRUD with SHA-256 content hash.

    Lifecycle-managed ROS2 node.  The scenario directory is declared as a
    parameter in ``on_configure`` and defaulted to ``/var/sil/scenarios``.
    A ``String`` publisher on ``/sil/scenario_loaded`` fires once per
    ``create_scenario()`` call — strictly event-driven, no timer loop.
    """

    def __init__(self, scenario_dir: str | None = None) -> None:
        super().__init__("scenario_authoring_node")
        self._loaded_pub: rclpy.publisher.Publisher | None = None
        if scenario_dir is not None:
            # Backward-compat for existing pure-Python tests (tests/sil/)
            self._dir = Path(scenario_dir)
            self._dir.mkdir(parents=True, exist_ok=True)
        else:
            self._dir = None  # set in on_configure

    # ------------------------------------------------------------------
    # Lifecycle callbacks
    # ------------------------------------------------------------------

    def on_configure(self, state) -> TransitionCallbackReturn:
        self.declare_parameter("scenario_dir", "/var/sil/scenarios")
        scenario_dir = (
            self.get_parameter("scenario_dir")
            .get_parameter_value()
            .string_value
        )
        self._dir = Path(scenario_dir)
        self._dir.mkdir(parents=True, exist_ok=True)
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state) -> TransitionCallbackReturn:
        self._loaded_pub = self.create_lifecycle_publisher(
            String, "/sil/scenario_loaded", 10
        )
        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state) -> TransitionCallbackReturn:
        if self._loaded_pub is not None:
            self.destroy_lifecycle_publisher(self._loaded_pub)
            self._loaded_pub = None
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state) -> TransitionCallbackReturn:
        return TransitionCallbackReturn.SUCCESS

    # ------------------------------------------------------------------
    # YAML CRUD operations  (preserved from pure-Python stub)
    # ------------------------------------------------------------------

    def list_scenarios(self) -> list[dict]:
        """Return a summary dict for every .yaml file in the scenario dir."""
        results: list[dict] = []
        for f in sorted(self._dir.glob("*.yaml")):
            results.append({
                "id": f.stem,
                "name": f.stem.replace("-", " ").title(),
                "encounter_type": "head-on",
            })
        return results

    def get_scenario(self, scenario_id: str) -> dict | None:
        """Read a single scenario by ID (stem).  Returns None if missing."""
        path = self._dir / f"{scenario_id}.yaml"
        if not path.exists():
            return None
        content = path.read_text()
        return {
            "yaml_content": content,
            "hash": hashlib.sha256(content.encode()).hexdigest(),
        }

    def create_scenario(self, yaml_content: str) -> dict:
        """Persist a new scenario file, returning its ID and content hash.

        Also publishes a ``String`` message with the scenario ID on
        ``/sil/scenario_loaded`` (event-driven, no timer).
        """
        sid = uuid.uuid4().hex[:12]
        (self._dir / f"{sid}.yaml").write_text(yaml_content)
        result = {
            "scenario_id": sid,
            "hash": hashlib.sha256(yaml_content.encode()).hexdigest(),
        }
        # Event-driven notification
        if self._loaded_pub is not None:
            msg = String()
            msg.data = sid
            self._loaded_pub.publish(msg)
        return result

    def delete_scenario(self, scenario_id: str) -> bool:
        """Remove a scenario file by ID.  Returns False if not found."""
        path = self._dir / f"{scenario_id}.yaml"
        if not path.exists():
            return False
        path.unlink()
        return True


def main(args: list[str] | None = None) -> None:
    """ROS2 LifecycleNode entry point (console_scripts)."""
    rclpy.init(args=args)
    node = ScenarioAuthoringNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()
