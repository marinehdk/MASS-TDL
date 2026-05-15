"""Shared pytest fixtures for ROS2 LifecycleNode integration testing.

Provides session-scoped rclpy context, a function-scoped isolated executor,
and a factory fixture that runs any LifecycleNode through its four standard
state transitions (configure → activate → deactivate → cleanup).

Gracefully skips when rclpy is unavailable (e.g., macOS dev without ROS2),
so mock-based tests in sibling directories remain unaffected.
"""

import pytest

try:
    import rclpy
    from rclpy.executors import MultiThreadedExecutor
    from rclpy.lifecycle import State, TransitionCallbackReturn

    _ROS2_AVAILABLE = True
except ImportError:
    _ROS2_AVAILABLE = False


@pytest.fixture(scope="session")
def ros_context():
    """Session-scoped rclpy initialisation / shutdown.

    Calls ``rclpy.init()`` once per test session and ``rclpy.shutdown()``
    during session teardown.  Skipped automatically when rclpy is not
    installed so that mock-based tests in the same directory tree are
    unaffected.
    """
    if not _ROS2_AVAILABLE:
        pytest.skip("ROS2 not available — skipping ROS2 integration tests")
    rclpy.init()
    yield
    rclpy.shutdown()


@pytest.fixture
def isolated_executor(ros_context):
    """Function-scoped ``MultiThreadedExecutor`` with 4 worker threads.

    Spins down the executor after each test function completes, preventing
    stray callback interference between tests.
    """
    executor = MultiThreadedExecutor(num_threads=4)
    yield executor
    executor.shutdown()


@pytest.fixture
def lifecycle_node_test(ros_context):
    """Factory fixture that runs a LifecycleNode through all 4 state transitions.

    Returns a callable ``_run(node_cls, node_name=None)`` that:

    1. Instantiates ``node_cls(node_name=name)``
    2. Calls ``on_configure → on_activate → spin_once → on_deactivate → on_cleanup``
    3. Returns ``(configured, activated, deactivated, cleaned_up)`` — each a ``bool``
       indicating whether the corresponding callback returned ``TransitionCallbackReturn.SUCCESS``.

    Example
    -------
    .. code:: python

        def test_my_node(lifecycle_node_test):
            ok_cfg, ok_act, ok_deact, ok_cln = lifecycle_node_test(MyNode)
            assert ok_cfg is True
            assert ok_act is True
            assert ok_deact is True
            assert ok_cln is True
    """

    def _run(node_cls, node_name=None):
        name = node_name or "test_node"
        node = node_cls(node_name=name)
        configured = node.on_configure(State()) == TransitionCallbackReturn.SUCCESS
        activated = node.on_activate(State()) == TransitionCallbackReturn.SUCCESS
        rclpy.spin_once(node, timeout_sec=0.1)
        deactivated = node.on_deactivate(State()) == TransitionCallbackReturn.SUCCESS
        cleaned_up = node.on_cleanup(State()) == TransitionCallbackReturn.SUCCESS
        node.destroy_node()
        return configured, activated, deactivated, cleaned_up

    return _run
