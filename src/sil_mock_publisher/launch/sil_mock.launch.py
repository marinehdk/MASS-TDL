"""SIL mock publisher launch file (D1.3b through D2.4).

D2.5 migration: comment out SilMockNode, uncomment M1/M2 real nodes.
Topic names /l3/sat/data and /l3/m1/odd_state remain unchanged.
"""
from launch import LaunchDescription


def generate_launch_description() -> LaunchDescription:
    return LaunchDescription([])
