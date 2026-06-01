# tools/sil/conftest.py
"""Shared pytest options for tools/sil/ integration tests."""


def pytest_addoption(parser):
    try:
        parser.addoption(
            "--scenario",
            default="colreg-rule14-ho",
            help="Scenario ID to run for trace/e2e tests",
        )
    except ValueError:
        pass  # already registered by another conftest
