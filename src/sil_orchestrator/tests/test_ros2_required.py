"""W10C TDD: verify that when rclpy is unavailable, main raises RuntimeError.

Strategy: main.py exposes a _require_ros2() callable. Tests patch _HAS_RCLPY=False
and call _require_ros2() directly — avoids the import-time side effects of module
reload.
"""
import pytest


def test_require_ros2_raises_when_rclpy_missing(monkeypatch):
    """_require_ros2() must raise RuntimeError when _HAS_RCLPY is False."""
    import sil_orchestrator.main as main_mod

    monkeypatch.setattr(main_mod, "_HAS_RCLPY", False)

    with pytest.raises(RuntimeError) as exc_info:
        main_mod._require_ros2()

    msg = str(exc_info.value).lower()
    assert "rclpy" in msg or "ros2" in msg, (
        f"RuntimeError must mention rclpy or ROS2; got: {exc_info.value}"
    )


def test_require_ros2_passes_when_rclpy_present(monkeypatch):
    """_require_ros2() must not raise when _HAS_RCLPY is True."""
    import sil_orchestrator.main as main_mod

    monkeypatch.setattr(main_mod, "_HAS_RCLPY", True)

    # Should not raise
    main_mod._require_ros2()
