# tests/integration/sim_determinism/conftest.py
"""Pytest fixtures for determinism integration tests.

These tests require the Docker SIL stack to be running.
Mark with: pytest -m integration
"""
import pytest


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "integration: mark test as requiring docker SIL stack"
    )
