import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REQUIRED_FRONTEND_TOPICS = {
    "/l2/planned_route",
    "/l3/m5/avoidance_plan",
    "/fusion/tracked_targets",
}


def _topic_whitelists(path: Path) -> list[set[str]]:
    text = path.read_text(encoding="utf-8")
    matches = re.findall(r'topic_whitelist:=\"\[([^\]]*)\]\"', text)
    return [{topic.strip() for topic in match.split(",") if topic.strip()} for match in matches]


def test_foxglove_whitelist_exports_frontend_route_topics():
    for compose_path in (ROOT / "docker-compose.yml", ROOT / "docker-compose.a4000.yml"):
        whitelists = _topic_whitelists(compose_path)
        assert whitelists, f"{compose_path.name} should define foxglove topic_whitelist"
        exported_topics = set().union(*whitelists)
        missing = REQUIRED_FRONTEND_TOPICS - exported_topics
        assert not missing, f"{compose_path.name} topic_whitelist missing {sorted(missing)}"


def test_sil_nodes_image_builds_ais_twin_package():
    text = (ROOT / "docker" / "sil_nodes.Dockerfile").read_text(encoding="utf-8")

    assert "COPY src/sim_workbench/ais_twin" in text
    assert re.search(r"--packages-select[\s\S]*\bais_twin\b", text)
