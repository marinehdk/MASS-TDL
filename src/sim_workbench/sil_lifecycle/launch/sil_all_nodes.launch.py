"""Launch all 10 SIL LifecycleNodes with auto lifecycle activation.

lifecycle_mgr publishes /sim_clock @ 1kHz, driving the simulation.
Each LifecycleNode action auto-transitions to ACTIVE by default.
"""

from launch import LaunchDescription
from launch_ros.actions import LifecycleNode


def generate_launch_description():
    lifecycle_mgr = LifecycleNode(
        package="sil_lifecycle", executable="scenario_lifecycle_mgr",
        name="scenario_lifecycle_mgr", namespace="", output="screen",
        parameters=[{"tick_hz": 1000.0, "status_hz": 1.0}],
    )
    ship_dynamics = LifecycleNode(
        package="ship_dynamics", executable="ship_dynamics_node",
        name="ship_dynamics_node", namespace="", output="screen",
    )
    env_disturbance = LifecycleNode(
        package="env_disturbance", executable="env_disturbance_node",
        name="env_disturbance_node", namespace="", output="screen",
    )
    target_vessel = LifecycleNode(
        package="target_vessel", executable="target_vessel_node",
        name="target_vessel_node", namespace="", output="screen",
    )
    sensor_mock = LifecycleNode(
        package="sensor_mock", executable="sensor_mock_node",
        name="sensor_mock_node", namespace="", output="screen",
    )
    tracker_mock = LifecycleNode(
        package="tracker_mock", executable="tracker_mock_node",
        name="tracker_mock_node", namespace="", output="screen",
    )
    fault_injection = LifecycleNode(
        package="fault_injection", executable="fault_injection_node",
        name="fault_injection_node", namespace="", output="screen",
    )
    scoring = LifecycleNode(
        package="scoring", executable="scoring_node",
        name="scoring_node", namespace="", output="screen",
    )
    scenario_authoring = LifecycleNode(
        package="scenario_authoring", executable="scenario_authoring_node",
        name="scenario_authoring_node", namespace="", output="screen",
    )

    return LaunchDescription([
        lifecycle_mgr, ship_dynamics, env_disturbance, target_vessel,
        sensor_mock, tracker_mock, fault_injection, scoring, scenario_authoring,
    ])
