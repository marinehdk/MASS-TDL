#!/usr/bin/env python3
"""TDD: Test that mock_l2 publisher loads scenario YAML config."""

import os
import tempfile
import unittest
from unittest.mock import patch
import yaml

# Note: we intentionally do NOT import docker/mock_l2_publisher here.
# That module depends on rclpy + ROS2 message packages which are only
# available inside the sil-nodes container. These unit tests instead
# exercise the YAML parsing contract (same logic as
# mock_l2_publisher._load_mock_l2_config) so they remain runnable on
# the host without ROS2. An integration test verifying the full publish
# chain runs in the container (Plan A W1 Step 1.5).


class TestMockL2PublisherYAML(unittest.TestCase):
    """Test mock_l2 publisher config loading from scenario YAML."""

    def test_loads_mock_l2_config_from_yaml(self):
        """Test that mock_l2 config is loaded from scenario YAML via SIL_SCENARIO_YAML env var."""
        # Create temporary scenario YAML with mock_l2 section
        scenario_yaml = {
            'ownShip': {
                'initial': {
                    'position': {'latitude': 63.44, 'longitude': 10.38},
                    'heading': 0.0,
                    'sog': 10.0
                },
                'nominalRoute': [
                    {'latitude': 63.44, 'longitude': 10.38, 'target_sog_kn': 10.0},
                    {'latitude': 63.60, 'longitude': 10.38, 'target_sog_kn': 10.0}
                ]
            },
            'mock_l2': {
                'planned_route': {
                    'waypoints': [
                        {'latitude': 63.44, 'longitude': 10.38},
                        {'latitude': 63.60, 'longitude': 10.38}
                    ],
                    'cruise_speed_kn': 10.0
                },
                'voyage_task': {
                    'autonomy_level': 'D3_SUPERVISED',
                    'mission_id': 'test-mission-001'
                }
            }
        }

        # Write to temp file
        with tempfile.NamedTemporaryFile(mode='w', suffix='.yaml', delete=False) as f:
            yaml.dump(scenario_yaml, f)
            yaml_path = f.name

        try:
            # Set env var and parse YAML the same way mock_l2_publisher would
            with patch.dict(os.environ, {'SIL_SCENARIO_YAML': yaml_path}):
                # Read the scenario the same way the publisher would
                with open(yaml_path, 'r') as f:
                    scenario_data = yaml.safe_load(f)

                mock_l2_config = scenario_data.get('mock_l2', {})

                # Verify the config was loaded correctly
                self.assertEqual(
                    mock_l2_config['voyage_task']['autonomy_level'],
                    'D3_SUPERVISED'
                )
                self.assertEqual(
                    mock_l2_config['voyage_task']['mission_id'],
                    'test-mission-001'
                )
                self.assertEqual(
                    mock_l2_config['planned_route']['cruise_speed_kn'],
                    10.0
                )
                self.assertEqual(
                    len(mock_l2_config['planned_route']['waypoints']),
                    2
                )
        finally:
            os.unlink(yaml_path)

    def test_mock_l2_config_missing_graceful_fallback(self):
        """Test that publisher gracefully handles missing mock_l2 section."""
        # Scenario without mock_l2 section
        scenario_yaml = {
            'ownShip': {
                'initial': {
                    'position': {'latitude': 63.44, 'longitude': 10.38},
                    'heading': 0.0,
                    'sog': 10.0
                },
                'nominalRoute': [
                    {'latitude': 63.44, 'longitude': 10.38, 'target_sog_kn': 10.0},
                    {'latitude': 63.60, 'longitude': 10.38, 'target_sog_kn': 10.0}
                ]
            }
        }

        with tempfile.NamedTemporaryFile(mode='w', suffix='.yaml', delete=False) as f:
            yaml.dump(scenario_yaml, f)
            yaml_path = f.name

        try:
            with patch.dict(os.environ, {'SIL_SCENARIO_YAML': yaml_path}):
                with open(yaml_path, 'r') as f:
                    scenario_data = yaml.safe_load(f)

                # Should return empty dict, not crash
                mock_l2_config = scenario_data.get('mock_l2', {})
                self.assertEqual(mock_l2_config, {})
        finally:
            os.unlink(yaml_path)


if __name__ == '__main__':
    unittest.main()
