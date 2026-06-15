from pathlib import Path

import pytest

from sil_orchestrator.runtime.manifests import (
    RuntimeManifestError,
    load_plugin_manifests,
    load_runtime_profiles,
)
from sil_orchestrator.runtime.models import PluginRole, RuntimeMode


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


VALID_PLUGIN = """
id: hydro-fossen
role: hydrodynamics
label: Hydro Fossen
runtime: compose
compose:
  service: plugin-hydro-fossen
image:
  expected: mass-hydro-fossen:0.9.3
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 10
  required_topics:
    /ship/odometry: nav_msgs/msg/Odometry
  forbidden_topics:
    - /sil/actuator_cmd
freshness:
  ownship_ms: 500
health:
  required: true
evidence:
  include_logs_tail_lines: 80
"""

VALID_PROFILE = """
name: integration-local
mode: integration
target: local
tdl_domain_id: 42
plugin_roles:
  hydrodynamics: hydro-fossen
safety:
  single_instance_per_role: true
  forbid_low_level_control: true
  require_version_metadata: true
"""


def test_loads_plugin_manifests_and_profiles(tmp_path: Path):
    plugin_dir = tmp_path / "runtime_plugins"
    profile_dir = tmp_path / "runtime_profiles"
    write(
        plugin_dir / "hydro-fossen.yaml",
        """
id: hydro-fossen
role: hydrodynamics
label: Hydro Fossen
runtime: compose
compose:
  service: plugin-hydro-fossen
image:
  expected: mass-hydro-fossen:0.9.3
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 10
  required_topics:
    /ship/odometry: nav_msgs/msg/Odometry
  forbidden_topics:
    - /sil/actuator_cmd
freshness:
  ownship_ms: 500
health:
  required: true
evidence:
  include_logs_tail_lines: 80
""",
    )
    write(
        profile_dir / "integration-local.yaml",
        """
name: integration-local
mode: integration
target: local
tdl_domain_id: 42
plugin_roles:
  hydrodynamics: hydro-fossen
safety:
  single_instance_per_role: true
  forbid_low_level_control: true
  require_version_metadata: true
""",
    )

    plugins = load_plugin_manifests(plugin_dir)
    profiles = load_runtime_profiles(profile_dir, plugins)

    assert plugins["hydro-fossen"].role is PluginRole.HYDRODYNAMICS
    assert plugins["hydro-fossen"].compose.service == "plugin-hydro-fossen"
    assert profiles["integration-local"].mode is RuntimeMode.INTEGRATION
    assert (
        profiles["integration-local"].plugin_roles[PluginRole.HYDRODYNAMICS]
        == "hydro-fossen"
    )


def test_rejects_duplicate_plugin_id(tmp_path: Path):
    plugin_dir = tmp_path / "runtime_plugins"
    for name in ("a.yaml", "b.yaml"):
        write(
            plugin_dir / name,
            """
id: hydro-fossen
role: hydrodynamics
label: Hydro Fossen
runtime: compose
compose:
  service: plugin-hydro-fossen
image:
  expected: mass-hydro-fossen:0.9.3
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 10
  required_topics: {}
  forbidden_topics: []
freshness: {}
health:
  required: false
evidence:
  include_logs_tail_lines: 40
""",
        )

    with pytest.raises(RuntimeManifestError, match="duplicate plugin id"):
        load_plugin_manifests(plugin_dir)


def test_rejects_profile_referencing_unknown_plugin(tmp_path: Path):
    plugin_dir = tmp_path / "runtime_plugins"
    profile_dir = tmp_path / "runtime_profiles"
    write(
        profile_dir / "integration-local.yaml",
        """
name: integration-local
mode: integration
target: local
tdl_domain_id: 42
plugin_roles:
  hydrodynamics: missing-plugin
safety:
  single_instance_per_role: true
  forbid_low_level_control: true
  require_version_metadata: true
""",
    )

    with pytest.raises(RuntimeManifestError, match="unknown plugin"):
        load_runtime_profiles(profile_dir, load_plugin_manifests(plugin_dir))


def test_rejects_profile_role_mismatch(tmp_path: Path):
    plugin_dir = tmp_path / "runtime_plugins"
    profile_dir = tmp_path / "runtime_profiles"
    write(
        plugin_dir / "hydro-fossen.yaml",
        VALID_PLUGIN.replace("role: hydrodynamics", "role: route_l2"),
    )
    write(profile_dir / "integration-local.yaml", VALID_PROFILE)

    with pytest.raises(RuntimeManifestError, match="role mismatch"):
        load_runtime_profiles(profile_dir, load_plugin_manifests(plugin_dir))


def test_rejects_invalid_runtime_value(tmp_path: Path):
    plugin_dir = tmp_path / "runtime_plugins"
    write(
        plugin_dir / "hydro-fossen.yaml",
        VALID_PLUGIN.replace("runtime: compose", "runtime: typo"),
    )

    with pytest.raises(RuntimeManifestError, match="runtime"):
        load_plugin_manifests(plugin_dir)


@pytest.mark.parametrize(
    ("content", "message"),
    [
        (
            VALID_PLUGIN.replace("domain_id: 10", "domain_id: true"),
            "ros.domain_id",
        ),
        (
            VALID_PLUGIN.replace(
                "  required_topics:\n    /ship/odometry: nav_msgs/msg/Odometry",
                "  required_topics: []",
            ),
            "ros.required_topics",
        ),
        (
            VALID_PLUGIN.replace(
                "  forbidden_topics:\n    - /sil/actuator_cmd",
                "  forbidden_topics: {}",
            ),
            "ros.forbidden_topics",
        ),
    ],
)
def test_rejects_representative_wrong_field_types(
    tmp_path: Path, content: str, message: str
):
    plugin_dir = tmp_path / "runtime_plugins"
    write(plugin_dir / "hydro-fossen.yaml", content)

    with pytest.raises(RuntimeManifestError, match=message):
        load_plugin_manifests(plugin_dir)


def test_loaded_nested_mappings_reject_mutation(tmp_path: Path):
    plugin_dir = tmp_path / "runtime_plugins"
    profile_dir = tmp_path / "runtime_profiles"
    write(plugin_dir / "hydro-fossen.yaml", VALID_PLUGIN)
    write(profile_dir / "integration-local.yaml", VALID_PROFILE)

    plugins = load_plugin_manifests(plugin_dir)
    profiles = load_runtime_profiles(profile_dir, plugins)
    plugin = plugins["hydro-fossen"]
    profile = profiles["integration-local"]

    with pytest.raises(TypeError):
        profile.plugin_roles[PluginRole.FUSION] = "yougc-fusion"

    with pytest.raises(TypeError):
        plugin.ros.required_topics["/extra"] = "std_msgs/msg/String"

    with pytest.raises(TypeError):
        plugin.freshness["extra_ms"] = 1
