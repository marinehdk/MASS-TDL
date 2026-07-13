#!/usr/bin/env python3
"""Validate the repository contract for MASS-TDL Codex specialist agents."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys
import tempfile
import tomllib
from typing import Iterable


EXPECTED_SANDBOX = {
    "tdl_product_conops": "read-only",
    "tdl_decision_chain_engineer": "workspace-write",
    "tdl_colregs_m6_reasoner": "workspace-write",
    "tdl_m5_planner_engineer": "workspace-write",
    "tdl_ros2_integration_engineer": "workspace-write",
    "tdl_hmi_m8_frontend": "workspace-write",
    "tdl_devops_a4000_engineer": "workspace-write",
    "tdl_mechanical_implementer": "workspace-write",
    "tdl_gnc_contract_reviewer": "read-only",
    "tdl_m7_safety_reviewer": "read-only",
    "tdl_code_reviewer": "read-only",
    "tdl_cyber_reviewer": "read-only",
    "tdl_cert_evidence_engineer": "read-only",
    "tdl_sil_vv_engineer": "read-only",
    "tdl_spec_architect": "read-only",
}

REQUIRED_KEYS = {
    "name",
    "description",
    "model",
    "model_reasoning_effort",
    "sandbox_mode",
    "developer_instructions",
}
ALLOWED_MODELS = {"gpt-5.6-sol"}
ALLOWED_EFFORTS = {"medium", "high", "xhigh"}
ALLOWED_SANDBOXES = {"read-only", "workspace-write"}

COMMON_INSTRUCTION_PATTERNS = {
    "ownership boundary": r"\bowns?\b|\bownership\b",
    "prohibited work": r"\bmust not\b|\bprohibited\b|\bnon[- ]responsibilit",
    "evidence duty": r"\bevidence\b",
    "escalation rule": r"\bescalat",
    "completion contract": r"\bcompletion contract\b",
}

ROLE_INSTRUCTION_PATTERNS = {
    "tdl_product_conops": (r"\bprd\b", r"\bconops\b", r"\bodd\b"),
    "tdl_decision_chain_engineer": (r"\bm1\b", r"\bm2\b", r"\bm3\b", r"\bm4\b"),
    "tdl_colregs_m6_reasoner": (r"\bm6\b", r"\bcolregs\b"),
    "tdl_m5_planner_engineer": (r"\bm5\b", r"\bplanner\b"),
    "tdl_ros2_integration_engineer": (r"\bros2\b", r"\bqos\b"),
    "tdl_hmi_m8_frontend": (r"\bm8\b", r"\bhmi\b"),
    "tdl_devops_a4000_engineer": (r"\ba4000\b", r"\bdocker\b"),
    "tdl_mechanical_implementer": (r"\blow[- ]risk\b", r"\bmechanical\b"),
    "tdl_gnc_contract_reviewer": (r"\bl4\b", r"\bexecutab", r"\bindependent\b"),
    "tdl_m7_safety_reviewer": (r"\bm7\b", r"\bindependent\b"),
    "tdl_code_reviewer": (r"\bcode review", r"\bread[- ]only\b"),
    "tdl_cyber_reviewer": (r"\bsecurity\b|\bcyber", r"\bread[- ]only\b"),
    "tdl_cert_evidence_engineer": (r"\bcertif", r"\btraceab"),
    "tdl_sil_vv_engineer": (r"\bsil\b", r"\bfirst divergence\b", r"\bread[- ]only\b"),
    "tdl_spec_architect": (r"\bspec preflight\b", r"\bread[- ]only\b"),
}

AGENTS_ROUTING_PATTERNS = {
    "stage classification": r"stage classification|classify (?:the )?(?:request|task) by stage",
    "one write owner": r"unique write owner|one (?:future )?write owner|single write owner",
    "permission override policy": r"permission[s]? (?:and |/)?[^\n]{0,80}override|override[^\n]{0,80}permission",
    "model override policy": r"model (?:and |/)?[^\n]{0,80}override|override[^\n]{0,80}model",
    "Codex model availability policy": r"codex model override[^\n]{0,120}(?:exposed|available)[^\n]{0,80}(?:codex runtime|runtime)",
    "no-chain rule": r"no[- ]chain|no (?:free )?(?:chain )?delegation|subagents? (?:must )?return[^\n]{0,80}primary",
    "mandatory task header": r"mandatory task header|required task header",
    "completion contract": r"completion contract",
    "systematic debugging route": r"systematic debugging",
    "SIL first-divergence route": r"sil first[- ]divergence|first divergence",
}

ROUTING_FIELD_PATTERNS = {
    "routing block": r"required agent routing",
    "stage": r"\bstage\b",
    "primary owner": r"primary (?:agent )?owner",
    "independent reviewers": r"independent reviewers?",
    "write authorization": r"write (?:authorization|permission)",
    "model/reasoning override": r"model(?:/| and )reasoning override|model[^\n]{0,80}reasoning[^\n]{0,80}override",
    "routing reasons": r"routing reasons?|reasons? for (?:the )?route",
    "evidence contract": r"evidence contract",
}

ROUTING_POSITIVE_PATTERNS = {
    "primary-agent authority": r"primary agent[^\n]{0,100}(?:owns?|retains?|has)[^\n]{0,60}(?:routing|final authority|decision)",
    "preflight fixed stage": r"\bstage:[^\n]{0,40}PRE_SPEC_DISCOVERY",
    "preflight main owner": r"primary owner:[^\n]{0,40}MAIN_AGENT",
    "preflight no-write rule": r"write authorization:[^\n]{0,30}NONE",
    "M5 self-approval prohibition": r"m5[^\n]{0,100}(?:must not|cannot|may not|never)[^\n]{0,60}self[- ]approv|(?:must not|cannot|may not|never)[^\n]{0,60}m5[^\n]{0,60}self[- ]approv",
    "SIL production-edit prohibition": r"sil[^\n]{0,100}(?:must not|cannot|may not|never)[^\n]{0,80}(?:edit|modify)[^\n]{0,60}production|(?:must not|cannot|may not|never)[^\n]{0,80}sil[^\n]{0,80}(?:edit|modify)[^\n]{0,60}production",
    "reviewer read-only default": r"reviewers?[^\n]{0,80}(?:default|remain|must be)[^\n]{0,40}read-only",
    "no router-agent delegation": r"routing[^\n]{0,100}(?:must not|cannot|may not|never)[^\n]{0,80}(?:router[- ]agent|delegat)|(?:must not|cannot|may not|never)[^\n]{0,80}(?:delegat[^\n]{0,40}routing|router[- ]agent)",
}

SKILL_SCOPE_FORBIDDEN_PATTERNS = {
    "workspace-write leakage": r"\bworkspace-write\b",
}

ROUTING_FORBIDDEN_PATTERNS = {
    "multiple write owners allowed": r"(?:(?:a\s+)?pair\s+of|two|multiple|more(?:\s+than\s+|-than-)one|two\s+or\s+more)\s+write owners?\s+(?:(?:is|are|remain|are to be|must be)\s+)?(?!(?:not|never)\s)(?:acceptable|allowed|permitted|required|selected|assigned|chosen|designated)\b|(?:(?:a\s+)?pair\s+of|two|multiple|more(?:\s+than\s+|-than-)one|two\s+or\s+more)\s+write owners?\s+(?:may|can)(?!\s+not\b)(?:\s+be)?\s+(?:acceptable|allowed|permitted|selected|assigned|chosen|designated)\b|(?<!not )(?<!never )\b(?:allow|allows|allowed|permit|permits|permitted|require|requires|required)\b[^\n]{0,50}(?:(?:a\s+)?pair\s+of|two|multiple|more(?:\s+than\s+|-than-)one|two\s+or\s+more)\s+write owners?",
    "M5 self-approval allowed": r"m5[^\n]{0,80}(?:\bmay\b(?!\s+not\b)|\bcan\b(?!\s+not\b)|is (?:allowed|permitted|authorized) to)[^\n]{0,20}self[- ]approv|(?<!not )(?<!never )\b(?:allow|permit|authorize)\b[^\n]{0,50}m5[^\n]{0,50}self[- ]approv",
    "SIL production edits allowed": r"sil[^\n]{0,80}(?:\bmay\b(?!\s+not\b)|\bcan\b(?!\s+not\b)|is (?:allowed|permitted|authorized) to)[^\n]{0,30}(?:edit|modify)[^\n]{0,50}production|(?<!not )(?<!never )\b(?:allow|permit|authorize)\b[^\n]{0,50}sil[^\n]{0,50}(?:edit|modify)[^\n]{0,50}production",
    "reviewer write default": r"reviewers?\s+(?:(?:default to|use|receive|have|are|remain)\s+(?:a\s+)?)?workspace-write\s+(?:(?:as|is)\s+(?:the\s+)?)?(?:default|by default)\b|reviewers?\s+begin\s+with\s+workspace-write(?:\s+access)?\b|reviewer write default[^\n]{0,20}(?:is|:)[^\n]{0,20}workspace-write|workspace-write\s+(?:is\s+)?(?:allowed|permitted|required)\s+(?:for\s+)?reviewers?\s+by default|(?<!not )(?<!never )\b(?:allow|permit|require)(?:s|ted|d)?\b[^\n]{0,50}reviewers?[^\n]{0,30}workspace-write[^\n]{0,20}by default",
    "router-agent delegation": r"routing[^\n]{0,40}(?:(?:may|can)(?!\s+not\b)(?:\s+be)?|is(?:\s+allowed\s+to|\s+permitted\s+to)?)[^\n]{0,20}(?:delegat|assign)[^\n]{0,50}(?:router[- ]agent|routing agent)|(?<!not )(?<!never )\b(?:delegate|assign)\b[^\n]{0,60}routing[^\n]{0,60}(?:router[- ]agent|routing agent)|(?:router[- ]agent|routing agent)[^\n]{0,60}(?:owns?|handles?|receives?|is\s+responsible\s+for)[^\n]{0,40}routing",
}

OBSOLETE_TOKEN_PATTERNS = (
    ("obsolete router", re.compile(r"(?<![A-Za-z0-9_-])tdl[-_]router[-_]architect(?![A-Za-z0-9_-])", re.IGNORECASE)),
    ("retired ZCode skill", re.compile(r"(?<![A-Za-z0-9_-])tdl-code-review(?![A-Za-z0-9_-])", re.IGNORECASE)),
    ("retired ZCode skill", re.compile(r"(?<![A-Za-z0-9_-])tdl-sil-verify(?![A-Za-z0-9_-])", re.IGNORECASE)),
    ("retired ZCode skill", re.compile(r"(?<![A-Za-z0-9_-])tdl-avoidance-debug(?![A-Za-z0-9_-])", re.IGNORECASE)),
)


@dataclass(frozen=True)
class Diagnostic:
    code: str
    location: str
    message: str


class Validator:
    def __init__(self) -> None:
        self.diagnostics: list[Diagnostic] = []

    def error(self, code: str, location: Path | str, message: str) -> None:
        self.diagnostics.append(Diagnostic(code, str(location), message))

    def require_patterns(
        self,
        *,
        text: str,
        patterns: dict[str, str] | Iterable[tuple[str, str]],
        code: str,
        location: Path,
    ) -> None:
        for label, pattern in dict(patterns).items():
            if not re.search(pattern, text, flags=re.IGNORECASE | re.MULTILINE):
                self.error(code, location, f"missing {label}: /{pattern}/")

    def reject_patterns(
        self,
        *,
        text: str,
        patterns: dict[str, str] | Iterable[tuple[str, str]],
        code: str,
        location: Path,
    ) -> None:
        for label, pattern in dict(patterns).items():
            if re.search(pattern, text, flags=re.IGNORECASE | re.MULTILINE):
                self.error(code, location, f"forbidden {label}: /{pattern}/")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    default_root = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=default_root,
        help=f"repository root (default: {default_root})",
    )
    parser.add_argument(
        "--skill-dir",
        type=Path,
        help="spec-preflight skill directory; omit to skip personal-skill checks",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run isolated adversarial validator fixtures",
    )
    return parser.parse_args(argv)


def load_agent_files(validator: Validator, repo_root: Path) -> dict[str, tuple[Path, dict]]:
    agents_dir = repo_root / ".codex" / "agents"
    if not agents_dir.is_dir():
        validator.error("agents-dir-missing", agents_dir, "expected agent definition directory")
        for name in EXPECTED_SANDBOX:
            validator.error("agent-missing", agents_dir / f"{name.replace('_', '-')}.toml", f"missing role {name}")
        return {}

    loaded: dict[str, tuple[Path, dict]] = {}
    seen_names: dict[str, Path] = {}
    for path in sorted(agents_dir.glob("*.toml")):
        try:
            data = tomllib.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, tomllib.TOMLDecodeError) as exc:
            validator.error("toml-invalid", path, f"cannot parse TOML: {exc}")
            continue

        missing = sorted(REQUIRED_KEYS - data.keys())
        if missing:
            validator.error("keys-missing", path, f"missing required keys: {', '.join(missing)}")

        name = data.get("name")
        if not isinstance(name, str) or not name.strip():
            validator.error("name-invalid", path, "name must be a non-empty string")
            continue
        if name in seen_names:
            validator.error("name-duplicate", path, f"name {name!r} already defined by {seen_names[name]}")
        else:
            seen_names[name] = path
        expected_filename = f"{name.replace('_', '-')}.toml"
        if path.name != expected_filename:
            validator.error("filename-mismatch", path, f"role {name!r} must use filename {expected_filename!r}")
        loaded[name] = (path, data)

    for name in sorted(set(EXPECTED_SANDBOX) - set(loaded)):
        validator.error("agent-missing", agents_dir / f"{name.replace('_', '-')}.toml", f"missing role {name}")
    for name in sorted(set(loaded) - set(EXPECTED_SANDBOX)):
        validator.error("agent-unexpected", loaded[name][0], f"unexpected role {name}")
    return loaded


def validate_agent_contracts(validator: Validator, loaded: dict[str, tuple[Path, dict]]) -> None:
    for name, (path, data) in sorted(loaded.items()):
        if name not in EXPECTED_SANDBOX:
            continue
        for key in REQUIRED_KEYS:
            value = data.get(key)
            if key not in data:
                continue
            if not isinstance(value, str) or not value.strip():
                validator.error("value-invalid", path, f"{key} must be a non-empty string")

        model = data.get("model")
        effort = data.get("model_reasoning_effort")
        sandbox = data.get("sandbox_mode")
        if model not in ALLOWED_MODELS:
            validator.error("model-invalid", path, f"model must be one of {sorted(ALLOWED_MODELS)}, got {model!r}")
        if effort not in ALLOWED_EFFORTS:
            validator.error("effort-invalid", path, f"model_reasoning_effort must be one of {sorted(ALLOWED_EFFORTS)}, got {effort!r}")
        if sandbox not in ALLOWED_SANDBOXES:
            validator.error("sandbox-invalid", path, f"sandbox_mode must be one of {sorted(ALLOWED_SANDBOXES)}, got {sandbox!r}")
        expected_sandbox = EXPECTED_SANDBOX[name]
        if sandbox != expected_sandbox:
            validator.error("permission-mismatch", path, f"{name} must default to {expected_sandbox!r}, got {sandbox!r}")

        instructions = data.get("developer_instructions")
        if not isinstance(instructions, str):
            continue
        validator.require_patterns(
            text=instructions,
            patterns=COMMON_INSTRUCTION_PATTERNS,
            code="instructions-incomplete",
            location=path,
        )
        role_patterns = {
            f"role phrase {index + 1}": pattern
            for index, pattern in enumerate(ROLE_INSTRUCTION_PATTERNS[name])
        }
        validator.require_patterns(
            text=instructions,
            patterns=role_patterns,
            code="instructions-role-incomplete",
            location=path,
        )


def read_required(validator: Validator, path: Path, code: str) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        validator.error(code, path, f"cannot read required file: {exc}")
        return ""


def preprocess_markdown(text: str) -> str:
    """Remove Markdown regions that cannot carry active routing policy."""
    text = re.sub(
        r"<!--.*?-->",
        lambda match: "\n" * match.group(0).count("\n"),
        text,
        flags=re.DOTALL,
    )
    output: list[str] = []
    fence_character: str | None = None
    fence_length = 0
    for line in text.splitlines(keepends=True):
        if fence_character is None:
            opening = re.match(r"^[ ]{0,3}(`{3,}|~{3,})[^\n]*", line)
            if opening is None:
                output.append(line)
                continue
            fence_character = opening.group(1)[0]
            fence_length = len(opening.group(1))
            output.append("\n" if line.endswith("\n") else "")
            continue
        closing = re.match(
            rf"^[ ]{{0,3}}{re.escape(fence_character)}{{{fence_length},}}[ \t]*(?:\n)?$",
            line,
        )
        output.append("\n" if line.endswith("\n") else "")
        if closing is not None:
            fence_character = None
            fence_length = 0
    return "".join(output)


def extract_markdown_section(text: str, heading_pattern: str) -> str | None:
    lines = text.splitlines()
    for index, line in enumerate(lines):
        heading = re.match(r"^(#{1,6})\s+(.+?)\s*$", line)
        if heading is None or not re.search(heading_pattern, heading.group(2), re.IGNORECASE):
            continue
        level = len(heading.group(1))
        end = len(lines)
        for following in range(index + 1, len(lines)):
            next_heading = re.match(r"^(#{1,6})\s+", lines[following])
            if next_heading is not None and len(next_heading.group(1)) <= level:
                end = following
                break
        return "\n".join(lines[index:end])
    return None


def _markdown_table_cells(line: str) -> list[str]:
    stripped = line.strip()
    if not stripped.startswith("|") or not stripped.endswith("|"):
        return []
    return [cell.strip() for cell in stripped[1:-1].split("|")]


def _normalized_table_header(cell: str) -> str:
    return re.sub(r"\s+", " ", cell.strip(" `*_:").lower())


def _is_table_separator(cells: list[str]) -> bool:
    return bool(cells) and all(re.fullmatch(r":?-{3,}:?", cell) for cell in cells)


def _has_real_route_trigger(cell: str) -> bool:
    normalized = re.sub(r"\s+", " ", cell.strip(" `*_:")).lower()
    if not normalized:
        return False
    if normalized in {"-", "n/a", "na", "none", "tbd", "todo", "unknown"}:
        return False
    if re.search(
        r"\bdo\s+not\b|\bnot\b|\bnever\b|\bexcluded\b|\bprohibited\b|\bno\s+work\b",
        normalized,
    ):
        return False
    metadata_declaration = re.compile(
        r"^(?:(?:agent|role)\s+)?(?:label|inventory|metadata|registry|name)"
        r"\b"
    )
    return metadata_declaration.search(normalized) is None


def role_has_positive_route(section: str, role: str) -> bool:
    """Require role coverage in a structured Trigger/assignment + Role table."""
    lines = section.splitlines()
    for index in range(len(lines) - 2):
        headers = _markdown_table_cells(lines[index])
        separators = _markdown_table_cells(lines[index + 1])
        if not headers or len(headers) != len(separators):
            continue
        if not _is_table_separator(separators):
            continue
        normalized_headers = [_normalized_table_header(cell) for cell in headers]
        try:
            trigger_index = normalized_headers.index("trigger / assignment")
            role_index = normalized_headers.index("role")
        except ValueError:
            continue
        for row_line in lines[index + 2 :]:
            row = _markdown_table_cells(row_line)
            if not row:
                break
            if len(row) != len(headers):
                continue
            row_role = row[role_index].strip(" `*_")
            if row_role != role:
                continue
            if _has_real_route_trigger(row[trigger_index]):
                return True
    return False


def validate_agents_md(validator: Validator, repo_root: Path) -> str:
    path = repo_root / "AGENTS.md"
    text = read_required(validator, path, "agents-md-missing")
    policy_text = preprocess_markdown(text)
    section = extract_markdown_section(
        policy_text, r"(?:codex.*routing|routing.*codex)"
    )
    if section is None:
        validator.error(
            "routing-section-missing",
            path,
            "missing Markdown section whose heading identifies Codex routing",
        )
        section = ""
    for role in sorted(EXPECTED_SANDBOX):
        if not role_has_positive_route(section, role):
            validator.error(
                "route-missing",
                path,
                f"Codex routing section has no positive route association for {role}",
            )
    validator.require_patterns(
        text=section,
        patterns=AGENTS_ROUTING_PATTERNS,
        code="routing-policy-incomplete",
        location=path,
    )
    validator.reject_patterns(
        text=policy_text,
        patterns=ROUTING_FORBIDDEN_PATTERNS,
        code="routing-policy-forbidden",
        location=path,
    )
    return text


def validate_skill(validator: Validator, skill_dir: Path) -> list[tuple[Path, str]]:
    skill_path = skill_dir / "SKILL.md"
    template_path = skill_dir / "references" / "preflight-brief-template.md"
    architect_contract_path = skill_dir / "references" / "architect-contract.md"
    granularity_routing_path = skill_dir / "references" / "granularity-routing.md"
    documents = [
        (skill_path, read_required(validator, skill_path, "skill-file-missing")),
        (template_path, read_required(validator, template_path, "skill-template-missing")),
        (
            architect_contract_path,
            read_required(validator, architect_contract_path, "skill-reference-missing"),
        ),
        (
            granularity_routing_path,
            read_required(validator, granularity_routing_path, "skill-reference-missing"),
        ),
    ]
    for path, text in documents[:2]:
        policy_text = preprocess_markdown(text)
        section = extract_markdown_section(policy_text, r"required agent routing")
        if section is None:
            validator.error(
                "skill-routing-block-missing",
                path,
                "missing Required Agent Routing Markdown section",
            )
            section = ""
        validator.require_patterns(
            text=section,
            patterns=ROUTING_FIELD_PATTERNS,
            code="skill-routing-incomplete",
            location=path,
        )
        validator.require_patterns(
            text=section,
            patterns=ROUTING_POSITIVE_PATTERNS,
            code="skill-routing-semantics-missing",
            location=path,
        )
        validator.reject_patterns(
            text=policy_text,
            patterns=ROUTING_FORBIDDEN_PATTERNS,
            code="skill-routing-forbidden",
            location=path,
        )
    for path, text in documents:
        validator.reject_patterns(
            text=preprocess_markdown(text),
            patterns=SKILL_SCOPE_FORBIDDEN_PATTERNS,
            code="skill-scope-forbidden",
            location=path,
        )
    return documents


def validate_forbidden_references(
    validator: Validator,
    documents: Iterable[tuple[Path, str]],
) -> None:
    for path, text in documents:
        for label, pattern in OBSOLETE_TOKEN_PATTERNS:
            match = pattern.search(text)
            if match is not None:
                validator.error(
                    "obsolete-reference",
                    path,
                    f"{label} must be absent: {match.group(0)}",
                )


def validate_contracts(repo_root: Path, skill_dir: Path | None) -> Validator:
    validator = Validator()
    loaded = load_agent_files(validator, repo_root)
    validate_agent_contracts(validator, loaded)
    agents_text = validate_agents_md(validator, repo_root)

    checked_documents: list[tuple[Path, str]] = [(repo_root / "AGENTS.md", agents_text)]
    checked_documents.extend(
        (
            path,
            f"{data.get('description', '')}\n{data.get('developer_instructions', '')}",
        )
        for path, data in loaded.values()
    )
    if skill_dir is not None:
        checked_documents.extend(validate_skill(validator, skill_dir))
    validate_forbidden_references(validator, checked_documents)
    return validator


def _valid_agent_instructions() -> str:
    return """Owns its narrow role ownership boundary.
Must not modify work outside that boundary; prohibited changes are explicit.
Collect evidence and escalate unresolved cross-boundary questions.
Completion contract: report changed paths, commands, results, risks, and evidence paths.
PRD ConOps ODD M1 M2 M3 M4 M5 M6 M7 M8 COLREGs planner ROS2 QoS HMI.
A4000 Docker low-risk mechanical L4 executability independent code review read-only.
Security cyber certification traceability SIL first divergence Spec Preflight.
"""


def _valid_routing_block() -> str:
    return """## Required Agent Routing

The primary agent owns routing and retains final authority for the routing decision.
- Stage: PRE_SPEC_DISCOVERY
- Primary owner: MAIN_AGENT
- Independent reviewers:
- Write authorization: NONE
- Model/reasoning override:
- Routing reasons:
- Evidence contract:
- M5 must not self-approve GNC executability.
- SIL must not edit production behavior.
- Reviewers default to read-only.
- Routing must not be delegated to a router agent.
- Compatibility spelling `tdl-code-reviewer` is not a retired skill token.
"""


def _write_valid_fixture(root: Path) -> tuple[Path, Path]:
    repo_root = root / "repo"
    skill_dir = root / "skill"
    agents_dir = repo_root / ".codex" / "agents"
    template_dir = skill_dir / "references"
    agents_dir.mkdir(parents=True)
    template_dir.mkdir(parents=True)

    instructions = _valid_agent_instructions()
    for role, sandbox in EXPECTED_SANDBOX.items():
        path = agents_dir / f"{role.replace('_', '-')}.toml"
        path.write_text(
            f'name = "{role}"\n'
            f'description = "Route for {role}"\n'
            'model = "gpt-5.6-sol"\n'
            'model_reasoning_effort = "high"\n'
            f'sandbox_mode = "{sandbox}"\n'
            f'developer_instructions = """\n{instructions}"""\n',
            encoding="utf-8",
        )

    policy_lines = [
        "## TDL Codex Routing",
        "",
        "- Stage classification: classify the task by stage.",
        "- Unique write owner: select a single write owner.",
        "- Permission override policy: the primary agent may override permission per task.",
        "- Model override policy: the primary agent may override model per task.",
        "- Codex model override must use a model exposed by the current Codex runtime.",
        "- No-chain rule: subagents must return to the primary agent.",
        "- Mandatory task header: include all nine fields.",
        "- Completion contract: report commands, results, risks, and evidence.",
        "- Route concrete failures through systematic debugging.",
        "- Route SIL first-divergence analysis before design escalation.",
        "",
        "| Trigger / assignment | Role |",
        "| --- | --- |",
    ]
    for role, sandbox in EXPECTED_SANDBOX.items():
        association = "reviewer" if sandbox == "read-only" else "owner"
        policy_lines.append(
            f"| Domain work; assign as {association} with `{sandbox}` default. | `{role}` |"
        )
    (repo_root / "AGENTS.md").write_text("\n".join(policy_lines) + "\n", encoding="utf-8")

    routing_block = _valid_routing_block()
    (skill_dir / "SKILL.md").write_text(
        f"# Spec Preflight\n\n{routing_block}", encoding="utf-8"
    )
    (template_dir / "preflight-brief-template.md").write_text(
        f"# Preflight Brief Template\n\n{routing_block}", encoding="utf-8"
    )
    (template_dir / "architect-contract.md").write_text(
        "# Architect Contract\n\nAll preflight specialist requests are read-only.\n",
        encoding="utf-8",
    )
    (template_dir / "granularity-routing.md").write_text(
        "# Granularity Routing\n\nAll preflight specialist requests are read-only.\n",
        encoding="utf-8",
    )
    return repo_root, skill_dir


def _assert_diagnostic(
    diagnostics: list[Diagnostic],
    *,
    code: str,
    location_suffix: str | None = None,
) -> None:
    matches = [diagnostic for diagnostic in diagnostics if diagnostic.code == code]
    if location_suffix is not None:
        matches = [
            diagnostic
            for diagnostic in matches
            if diagnostic.location.endswith(location_suffix)
        ]
    if not matches:
        raise AssertionError(
            f"expected diagnostic {code!r} at {location_suffix!r}; got {diagnostics!r}"
        )


def _assert_no_diagnostic(
    diagnostics: list[Diagnostic],
    *,
    code: str,
    location_suffix: str,
) -> None:
    matches = [
        diagnostic
        for diagnostic in diagnostics
        if diagnostic.code == code
        and diagnostic.location.endswith(location_suffix)
    ]
    if matches:
        raise AssertionError(
            f"unexpected diagnostic {code!r} at {location_suffix!r}: {matches!r}"
        )


def run_self_tests() -> int:
    fixture_count = 0
    with tempfile.TemporaryDirectory(prefix="tdl-agent-validator-") as temporary:
        root = Path(temporary)
        repo_root, skill_dir = _write_valid_fixture(root)
        baseline = validate_contracts(repo_root, skill_dir).diagnostics
        fixture_count += 1
        if baseline:
            raise AssertionError(f"valid fixture produced diagnostics: {baseline!r}")

        role = "tdl_m5_planner_engineer"
        structured_route = f"""| Trigger / assignment | Role |
| --- | --- |
| M5 Tactical Planner implementation; assign as write owner. | `{role}` |
"""
        if not role_has_positive_route(structured_route, role):
            raise AssertionError("structured routing-table row was not accepted")

        repo_root, skill_dir = _write_valid_fixture(root / "fenced-route-table")
        agents_path = repo_root / "AGENTS.md"
        valid_role_row = (
            "| Domain work; assign as owner with `workspace-write` default. "
            f"| `{role}` |"
        )
        agents_path.write_text(
            agents_path.read_text(encoding="utf-8").replace(valid_role_row, "")
            + "\n```markdown\n"
            + structured_route
            + "```\n",
            encoding="utf-8",
        )
        _assert_diagnostic(
            validate_contracts(repo_root, skill_dir).diagnostics,
            code="route-missing",
            location_suffix="AGENTS.md",
        )
        fixture_count += 1

        negative_route_triggers = (
            "Do not assign any work to this role; it is excluded.",
            "This role is not assigned M5 work.",
            "Never assign M5 work to this role.",
            "This role is excluded from M5 work.",
            "This role is prohibited from M5 work.",
            "No work may be assigned to this role.",
        )
        for case_index, trigger in enumerate(negative_route_triggers):
            repo_root, skill_dir = _write_valid_fixture(
                root / f"negative-route-row-{case_index}"
            )
            agents_path = repo_root / "AGENTS.md"
            agents_path.write_text(
                agents_path.read_text(encoding="utf-8").replace(
                    valid_role_row,
                    f"| {trigger} | `{role}` |",
                ),
                encoding="utf-8",
            )
            _assert_diagnostic(
                validate_contracts(repo_root, skill_dir).diagnostics,
                code="route-missing",
                location_suffix="AGENTS.md",
            )
            fixture_count += 1

        reviewer_non_routes = (
            f"- Agent label: use `{role}` for M5 work.",
            f"- Agent inventory: route M5 work to `{role}`.",
            f"- Agent metadata: `{role}` is the write owner.",
            f"- Role registry: `{role}` owns M5 work.",
            f"- Agent name: assign M5 work to `{role}`.",
            f"""| Trigger / assignment | Role |
| --- | --- |
| Inventory record for M5 agents | `{role}` |
""",
            f"""| Trigger / assignment | Role |
| --- | --- |
| TBD | `{role}` |
""",
        )
        for non_route in reviewer_non_routes:
            fixture_count += 1
            if role_has_positive_route(non_route, role):
                raise AssertionError(
                    f"metadata/prose declaration counted as a route: {non_route!r}"
                )

        opposite_cases = (
            ("Exactly one write owner only.", "Multiple write owners are allowed."),
            (
                "Exactly one write owner only.",
                "More than one write owner allowed.",
            ),
            (
                "M5 must not self-approve",
                "M5 may self-approve GNC executability.",
            ),
            ("SIL must not edit", "SIL may edit production behavior."),
            ("Reviewers default to read-only.", "Reviewers use a workspace-write default."),
            (
                "Reviewers default to read-only.",
                "Reviewers workspace-write by default.",
            ),
            (
                "Routing must not be delegated to a router agent.",
                "Routing may be delegated to a router agent.",
            ),
        )
        for case_index, (_, opposite) in enumerate(opposite_cases):
            for document_name, location_suffix in (
                ("SKILL.md", "SKILL.md"),
                (
                    "references/preflight-brief-template.md",
                    "preflight-brief-template.md",
                ),
            ):
                repo_root, skill_dir = _write_valid_fixture(
                    root / f"mixed-opposite-{case_index}-{location_suffix}"
                )
                document_path = skill_dir / document_name
                document_path.write_text(
                    document_path.read_text(encoding="utf-8")
                    + f"\n# Unrelated appendix\n\n- Unsafe compatibility exception: {opposite}\n",
                    encoding="utf-8",
                )
                diagnostics = validate_contracts(repo_root, skill_dir).diagnostics
                fixture_count += 1
                _assert_diagnostic(
                    diagnostics,
                    code="skill-routing-forbidden",
                    location_suffix=location_suffix,
                )

        whole_document_opposites = (
            "A pair of write owners is acceptable.",
            "Two write owners are allowed.",
            "More-than-one write owner is acceptable.",
            "M5 is authorized to self-approve GNC executability.",
            "The SIL verifier is authorized to modify production behavior.",
            "Reviewers begin with workspace-write access.",
            "The routing agent is responsible for routing.",
            "More than one write owner allowed.",
            "More than one write owner permitted.",
            "More than one write owner required.",
            "Allow more than one write owner.",
            "Permit multiple write owners.",
            "Require two or more write owners.",
            "Reviewers workspace-write by default.",
            "Reviewers use workspace-write by default.",
            "Workspace-write is permitted for reviewers by default.",
            "Require reviewers workspace-write by default.",
        )
        whole_document_cases = (
            ("AGENTS.md", "AGENTS.md", "routing-policy-forbidden"),
            ("SKILL.md", "SKILL.md", "skill-routing-forbidden"),
            (
                "references/preflight-brief-template.md",
                "preflight-brief-template.md",
                "skill-routing-forbidden",
            ),
        )
        for case_index, opposite in enumerate(whole_document_opposites):
            for document_name, location_suffix, code in whole_document_cases:
                repo_root, skill_dir = _write_valid_fixture(
                    root
                    / f"whole-document-opposite-{case_index}-{location_suffix}"
                )
                if document_name == "AGENTS.md":
                    document_path = repo_root / document_name
                else:
                    document_path = skill_dir / document_name
                document_path.write_text(
                    document_path.read_text(encoding="utf-8")
                    + f"\n# Unrelated appendix\n\n{opposite}\n",
                    encoding="utf-8",
                )
                diagnostics = validate_contracts(repo_root, skill_dir).diagnostics
                fixture_count += 1
                _assert_diagnostic(
                    diagnostics,
                    code=code,
                    location_suffix=location_suffix,
                )

        fenced_section_cases = (
            ("AGENTS.md", "AGENTS.md", "routing-section-missing"),
            ("SKILL.md", "SKILL.md", "skill-routing-block-missing"),
            (
                "references/preflight-brief-template.md",
                "preflight-brief-template.md",
                "skill-routing-block-missing",
            ),
        )
        for case_index, (document_name, location_suffix, code) in enumerate(
            fenced_section_cases
        ):
            repo_root, skill_dir = _write_valid_fixture(
                root / f"fenced-required-section-{case_index}"
            )
            if document_name == "AGENTS.md":
                document_path = repo_root / document_name
                fenced_content = document_path.read_text(encoding="utf-8")
            else:
                document_path = skill_dir / document_name
                fenced_content = _valid_routing_block()
            document_path.write_text(
                "# Documentation example\n\n```markdown\n"
                + fenced_content
                + "```\n",
                encoding="utf-8",
            )
            _assert_diagnostic(
                validate_contracts(repo_root, skill_dir).diagnostics,
                code=code,
                location_suffix=location_suffix,
            )
            fixture_count += 1

        for case_index, (document_name, location_suffix, code) in enumerate(
            fenced_section_cases
        ):
            repo_root, skill_dir = _write_valid_fixture(
                root / f"commented-required-section-{case_index}"
            )
            if document_name == "AGENTS.md":
                document_path = repo_root / document_name
                commented_content = document_path.read_text(encoding="utf-8")
            else:
                document_path = skill_dir / document_name
                commented_content = _valid_routing_block()
            document_path.write_text(
                "# Documentation example\n\n<!--\n"
                + commented_content
                + "-->\n",
                encoding="utf-8",
            )
            _assert_diagnostic(
                validate_contracts(repo_root, skill_dir).diagnostics,
                code=code,
                location_suffix=location_suffix,
            )
            fixture_count += 1

        hidden_opposites = "\n".join(whole_document_opposites)
        for wrapper_name, wrapped in (
            ("fenced", f"```text\n{hidden_opposites}\n```\n"),
            ("commented", f"<!--\n{hidden_opposites}\n-->\n"),
        ):
            for document_name, location_suffix, code in whole_document_cases:
                repo_root, skill_dir = _write_valid_fixture(
                    root / f"hidden-opposites-{wrapper_name}-{location_suffix}"
                )
                if document_name == "AGENTS.md":
                    document_path = repo_root / document_name
                else:
                    document_path = skill_dir / document_name
                document_path.write_text(
                    document_path.read_text(encoding="utf-8") + "\n" + wrapped,
                    encoding="utf-8",
                )
                _assert_no_diagnostic(
                    validate_contracts(repo_root, skill_dir).diagnostics,
                    code=code,
                    location_suffix=location_suffix,
                )
                fixture_count += 1

        repo_root, skill_dir = _write_valid_fixture(root / "safe-whole-document")
        for document_name, location_suffix, code in whole_document_cases:
            if document_name == "AGENTS.md":
                document_path = repo_root / document_name
            else:
                document_path = skill_dir / document_name
            safe_appendix = "More than one write owner is not allowed.\n"
            if document_name == "AGENTS.md":
                safe_appendix += "Reviewers must not use workspace-write by default.\n"
            else:
                safe_appendix += "Reviewers must remain read-only.\n"
            document_path.write_text(
                document_path.read_text(encoding="utf-8")
                + "\n# Unrelated appendix\n\n"
                + safe_appendix,
                encoding="utf-8",
            )
            diagnostics = validate_contracts(repo_root, skill_dir).diagnostics
            fixture_count += 1
            _assert_no_diagnostic(
                diagnostics,
                code=code,
                location_suffix=location_suffix,
            )

        repo_root, skill_dir = _write_valid_fixture(root / "safe-negatives")
        for path in (
            skill_dir / "SKILL.md",
            skill_dir / "references" / "preflight-brief-template.md",
        ):
            text = path.read_text(encoding="utf-8")
            text = text.replace("M5 must not self-approve", "M5 may not self-approve")
            text = text.replace("SIL must not edit", "SIL cannot edit")
            text = text.replace(
                "Reviewers default to read-only.", "Reviewers must be read-only."
            )
            path.write_text(text, encoding="utf-8")
        safe_negative_diagnostics = validate_contracts(repo_root, skill_dir).diagnostics
        fixture_count += 1
        if safe_negative_diagnostics:
            raise AssertionError(
                "safe negative policies were rejected: "
                f"{safe_negative_diagnostics!r}"
            )

        non_routes = (
            f"- Inventory token: `{role}`.",
            f"- Domain token: `{role}`; M5 planner domain.",
            f"- Do not route domain work to `{role}`.",
            f"- `{role}` cannot be assigned as owner.",
            f"- `{role}` may not be owner.",
            f"- `{role}` is prohibited from routing ownership.",
            f"- `{role}` is excluded from routing.",
            f"- `{role}` is not owner.",
        )
        for case_index, non_route in enumerate(non_routes):
            repo_root, skill_dir = _write_valid_fixture(
                root / f"role-association-{case_index}"
            )
            agents_path = repo_root / "AGENTS.md"
            agents_path.write_text(
                "\n".join(
                    non_route if role in line else line
                    for line in agents_path.read_text(encoding="utf-8").splitlines()
                )
                + "\n",
                encoding="utf-8",
            )
            fixture_count += 1
            _assert_diagnostic(
                validate_contracts(repo_root, skill_dir).diagnostics,
                code="route-missing",
                location_suffix="AGENTS.md",
            )

        repo_root, skill_dir = _write_valid_fixture(root / "line-boundary")
        template_path = skill_dir / "references" / "preflight-brief-template.md"
        template_path.write_text(
            template_path.read_text(encoding="utf-8").replace(
                "Model/reasoning override:", "Model\nreasoning override:"
            ),
            encoding="utf-8",
        )
        _assert_diagnostic(
            validate_contracts(repo_root, skill_dir).diagnostics,
            code="skill-routing-incomplete",
            location_suffix="preflight-brief-template.md",
        )
        fixture_count += 1

        repo_root, skill_dir = _write_valid_fixture(root / "token-boundary")
        if validate_contracts(repo_root, skill_dir).diagnostics:
            raise AssertionError("legitimate tdl-code-reviewer alias was rejected")
        fixture_count += 1
        skill_path = skill_dir / "SKILL.md"
        skill_path.write_text(
            skill_path.read_text(encoding="utf-8") + "\nRetired: tdl-code-review\n",
            encoding="utf-8",
        )
        _assert_diagnostic(
            validate_contracts(repo_root, skill_dir).diagnostics,
            code="obsolete-reference",
            location_suffix="SKILL.md",
        )
        fixture_count += 1

        repo_root, skill_dir = _write_valid_fixture(root / "independent-docs")
        template_path = skill_dir / "references" / "preflight-brief-template.md"
        template_path.write_text(
            template_path.read_text(encoding="utf-8").replace("- Evidence contract:\n", ""),
            encoding="utf-8",
        )
        _assert_diagnostic(
            validate_contracts(repo_root, skill_dir).diagnostics,
            code="skill-routing-incomplete",
            location_suffix="preflight-brief-template.md",
        )
        fixture_count += 1

        field_removals = {
            "routing block": "## Required Agent Routing\n",
            "stage": "- Stage: PRE_SPEC_DISCOVERY\n",
            "primary owner": "- Primary owner: MAIN_AGENT\n",
            "independent reviewers": "- Independent reviewers:\n",
            "write authorization": "- Write authorization: NONE\n",
            "model/reasoning override": "- Model/reasoning override:\n",
            "routing reasons": "- Routing reasons:\n",
            "evidence contract": "- Evidence contract:\n",
        }
        semantic_removals = {
            "primary-agent authority": (
                "The primary agent owns routing and retains final authority for the routing decision.\n"
            ),
            "preflight fixed stage": "- Stage: PRE_SPEC_DISCOVERY\n",
            "preflight main owner": "- Primary owner: MAIN_AGENT\n",
            "preflight no-write rule": "- Write authorization: NONE\n",
            "M5 self-approval prohibition": (
                "- M5 must not self-approve GNC executability.\n"
            ),
            "SIL production-edit prohibition": (
                "- SIL must not edit production behavior.\n"
            ),
            "reviewer read-only default": "- Reviewers default to read-only.\n",
            "no router-agent delegation": (
                "- Routing must not be delegated to a router agent.\n"
            ),
        }
        document_cases = (
            ("SKILL.md", "SKILL.md", "preflight-brief-template.md"),
            (
                "references/preflight-brief-template.md",
                "preflight-brief-template.md",
                "SKILL.md",
            ),
        )
        for code, removals in (
            ("skill-routing-incomplete", field_removals),
            ("skill-routing-semantics-missing", semantic_removals),
        ):
            for label, removal in removals.items():
                for document_name, location_suffix, sibling_suffix in document_cases:
                    repo_root, skill_dir = _write_valid_fixture(
                        root
                        / f"symmetric-removal-{code}-{label}-{location_suffix}"
                    )
                    document_path = skill_dir / document_name
                    original = document_path.read_text(encoding="utf-8")
                    if removal not in original:
                        raise AssertionError(
                            f"fixture removal target absent for {label}: {removal!r}"
                        )
                    document_path.write_text(
                        original.replace(removal, "", 1), encoding="utf-8"
                    )
                    diagnostics = validate_contracts(repo_root, skill_dir).diagnostics
                    fixture_count += 1
                    _assert_diagnostic(
                        diagnostics,
                        code=code,
                        location_suffix=location_suffix,
                    )
                    _assert_no_diagnostic(
                        diagnostics,
                        code=code,
                        location_suffix=sibling_suffix,
                    )

        for reference_name in ("architect-contract.md", "granularity-routing.md"):
            repo_root, skill_dir = _write_valid_fixture(
                root / f"reference-scope-{reference_name}"
            )
            reference_path = skill_dir / "references" / reference_name
            reference_path.write_text(
                reference_path.read_text(encoding="utf-8")
                + "\nLater stage may use workspace-write.\n",
                encoding="utf-8",
            )
            diagnostics = validate_contracts(repo_root, skill_dir).diagnostics
            fixture_count += 1
            _assert_diagnostic(
                diagnostics,
                code="skill-scope-forbidden",
                location_suffix=reference_name,
            )

    print(f"SELF-TEST PASS: {fixture_count} adversarial routing fixtures")
    return 0


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.self_test:
        return run_self_tests()

    repo_root = args.repo_root.expanduser().resolve()
    skill_dir = args.skill_dir.expanduser().resolve() if args.skill_dir is not None else None
    validator = validate_contracts(repo_root, skill_dir)

    for diagnostic in sorted(
        validator.diagnostics,
        key=lambda item: (item.location, item.code, item.message),
    ):
        print(
            f"ERROR [{diagnostic.code}] {diagnostic.location}: {diagnostic.message}",
            file=sys.stderr,
        )
    if validator.diagnostics:
        print(f"FAILED: {len(validator.diagnostics)} routing contract violation(s)", file=sys.stderr)
        return 1

    print(f"OK: validated {len(EXPECTED_SANDBOX)} TDL agent definitions and routing contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
