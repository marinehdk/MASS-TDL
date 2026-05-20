"""l3_pipeline.launch.py — DEMO-1 full L3 kernel pipeline launch.

Launches all M1–M8 nodes in correct dependency order.
M7 Safety Supervisor runs in a separate GroupAction (PATH-S independence).

Usage:
    ros2 launch src/l3_tdl_kernel/launch/l3_pipeline.launch.py
    ros2 launch src/l3_tdl_kernel/launch/l3_pipeline.launch.py enable_m7:=false
"""
import os

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, GroupAction
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    # Locate params file relative to this launch script
    _launch_dir = os.path.realpath(os.path.dirname(__file__))

    # ── Launch Arguments ──────────────────────────────────────────────

    declare_params_file = DeclareLaunchArgument(
        'params_file',
        default_value=os.path.join(_launch_dir, 'l3_params.yaml'),
        description='Path to L3 pipeline parameter YAML file',
    )

    declare_enable_m7 = DeclareLaunchArgument(
        'enable_m7',
        default_value='true',
        description='Enable M7 Safety Supervisor (PATH-S). Set false for Doer-only testing.',
    )

    # ── M1  ODD / Envelope Manager ────────────────────────────────────
    # Establishes the operational envelope. All other modules reference the
    # current ODD state for context-appropriate behaviour.
    m1_odd = Node(
        package='m1_odd_envelope_manager',
        executable='m1_odd_envelope_manager',
        name='m1_odd_manager',
        output='screen',
        parameters=[LaunchConfiguration('params_file')],
    )

    # ── M2  World Model ───────────────────────────────────────────────
    # Fuses sensor data into a unified world view (static + dynamic + own
    # ship state). Provides COLREG geometric pre-classification.
    m2_world = Node(
        package='m2_world_model',
        executable='m2_world_model',
        name='m2_world_model',
        output='screen',
        parameters=[LaunchConfiguration('params_file')],
    )

    # ── M3  Mission Manager ───────────────────────────────────────────
    # Loads the voyage plan from L2 and tracks progress. Triggers
    # re-planning on deviation.
    m3_mission = Node(
        package='m3_mission_manager',
        executable='m3_mission_manager',
        name='m3_mission_manager',
        output='screen',
        parameters=[LaunchConfiguration('params_file')],
    )

    # ── M6  COLREGs Reasoner ──────────────────────────────────────────
    # Rule engine that evaluates encounter geometry against ODD-aware
    # COLREG rules. Must start before M4 so constraints are available.
    m6_colregs = Node(
        package='m6_colregs_reasoner',
        executable='m6_colregs_reasoner',
        name='m6_colregs_reasoner',
        output='screen',
        parameters=[LaunchConfiguration('params_file')],
    )

    # ── M4  Behaviour Arbiter ─────────────────────────────────────────
    # Interval Programming (IvP) multi-objective arbiter. Consumes
    # COLREGs constraints from M6 and behaviour plans from M3.
    m4_arbiter = Node(
        package='m4_behavior_arbiter',
        executable='m4_behavior_arbiter',
        name='m4_behavior_arbiter',
        output='screen',
        parameters=[LaunchConfiguration('params_file')],
    )

    # ── M5  Tactical Planner (Mid-MPC) ────────────────────────────────
    # Mid-horizon MPC producing avoidance waypoints. Consumes the
    # arbitrated behaviour plan from M4.
    m5_planner = Node(
        package='m5_tactical_planner',
        executable='m5_mid_mpc_node',
        name='m5_tactical_planner',
        output='screen',
        parameters=[LaunchConfiguration('params_file')],
    )

    # ── M8  HMI / Transparency Bridge ─────────────────────────────────
    # Aggregates SAT data from M1–M6 and drives the ROC HMI. Independent
    # of M7 (no shared code path).
    m8_hmi = Node(
        package='m8_hmi_transparency_bridge',
        executable='m8_hmi_transparency_bridge_node',
        name='m8_hmi_bridge',
        output='screen',
        parameters=[LaunchConfiguration('params_file')],
    )

    # ── M7  Safety Supervisor (Checker — PATH-S independent) ──────────
    # M7 runs in its own GroupAction with scoped=True to emphasise
    # architectural Doer-Checker separation (ADR-001, Decision 4).
    # At runtime this node occupies a separate OS process with no shared
    # code or libraries with M1–M6 or M8.
    # The IfCondition allows disabling the Checker for PATH-S testing.
    m7_safety_group = GroupAction(
        scoped=True,
        condition=IfCondition(LaunchConfiguration('enable_m7')),
        actions=[
            Node(
                package='m7_safety_supervisor',
                executable='m7_safety_supervisor',
                name='m7_safety_supervisor',
                output='screen',
                parameters=[LaunchConfiguration('params_file')],
            ),
        ],
    )

    return LaunchDescription([
        # Arguments first
        declare_params_file,
        declare_enable_m7,
        # Doer pipeline (M1–M6 + M8)
        m1_odd,
        m2_world,
        m3_mission,
        m6_colregs,
        m4_arbiter,
        m5_planner,
        m8_hmi,
        # Checker (PATH-S independent)
        m7_safety_group,
    ])
