"""Session-scoped ROS 2 context fixture for MASS L3 integration tests."""
import pytest
import rclpy


@pytest.fixture(scope="session", autouse=True)
def ros_context():
    """Initialize and shut down the ROS 2 context for the entire test session."""
    if not rclpy.ok():
        rclpy.init()
    try:
        yield
    finally:
        rclpy.shutdown()
