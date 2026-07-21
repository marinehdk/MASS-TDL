#!/usr/bin/env python3
"""X4 Failure Classifier -- automated root-cause classification of solver failures.

Four failure categories (deterministic, vessel-agnostic):
  1. Original OCP Infeasible  -- raw MAXITER with low sqp_iter; constraint set
                                 fundamentally impossible.
  2. Linearized QP Infeasible -- QP subproblem infeasible (MINSTEP, or MAXITER
                                 with high sqp_iter indicating QP struggle).
  3. Numerical Non-Convergence -- NAN_DETECTED, QP_FAILURE, READY, UNBOUNDED,
                                 or TIMEOUT.
  4. Output Non-Executable     -- Solver converged (SUCCESS) but trajectory
                                 fails tail-gate checks (constraint violations,
                                 GNC executability rejection).

Usage as library:
    from x4_failure_classifier import classify, Classification

    result = classify(solver_output)
    # result is a Classification namedtuple/dict with .category, .confidence, etc.

Usage as CLI:
    python3 x4_failure_classifier.py \
        --solution solution.json \
        --verdict verdict.json \
        [--derivative-diag derivative_diagnostics.json] \
        [--gnc-exec gnc_executability.json] \
        [--output -]
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any, Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# Acados raw status semantics (from acados/utils/types.h, verified in
# verify_runtime_contract.py)
# ---------------------------------------------------------------------------
ACADOS_STATUS = {
    0: "ACADOS_SUCCESS",
    1: "ACADOS_NAN_DETECTED",
    2: "ACADOS_MAXITER",
    3: "ACADOS_MINSTEP",
    4: "ACADOS_QP_FAILURE",
    5: "ACADOS_READY",
    6: "ACADOS_UNBOUNDED",
    7: "ACADOS_TIMEOUT",
}

# ---------------------------------------------------------------------------
# Category constants
# ---------------------------------------------------------------------------
CATEGORY_NAMES = {
    1: "Original OCP Infeasible",
    2: "Linearized QP Infeasible",
    3: "Numerical Non-Convergence",
    4: "Output Non-Executable",
}

# SQP iteration threshold for classifying MAXITER between OCP vs QP infeasibility.
# Low sqp_iter: solver barely started -- OCP likely fundamentally infeasible.
# High sqp_iter: SQP made progress but QP subproblems kept failing.
MAXITER_OCP_VS_QP_THRESHOLD = 10


# ---------------------------------------------------------------------------
# Output type
# ---------------------------------------------------------------------------
class Classification(dict):
    """Immutable-ish classification result with attribute access."""

    def __init__(self, category: int, confidence: float, reasoning: str,
                 evidence: Dict[str, Any]):
        data: Dict[str, Any] = {
            "category": category,
            "category_name": CATEGORY_NAMES.get(category, "Unknown"),
            "confidence": confidence,
            "reasoning": reasoning,
            "evidence": evidence,
        }
        super().__init__(data)

    def __getattr__(self, name: str) -> Any:
        try:
            return self[name]
        except KeyError:
            raise AttributeError(name)

    def __setattr__(self, name: str, value: Any) -> None:
        self[name] = value

    def __repr__(self) -> str:
        return (f"Classification(category={self['category']}, "
                f"name='{self['category_name']}', "
                f"confidence={self['confidence']:.2f})")


# ---------------------------------------------------------------------------
# Core classifier
# ---------------------------------------------------------------------------
def classify(solver_output: Dict[str, Any]) -> Classification:
    """Classify a solver failure into one of four root-cause categories.

    Parameters
    ----------
    solver_output : dict
        Must contain at minimum:
          - raw_status: int  (acados return code 0-7)
        Strongly recommended:
          - sqp_iter: int
          - has_constraint_violations: bool  (post-solve trajectory check)
          - gnc_executability: str or None  ("PASS", "FAIL", "OPEN", None)
        Optional (increase confidence):
          - qp_status_history: list[int]
          - nlp_residuals: list[float]
          - cost: float
          - raw_semantic: str
          - qp_iter_history: list[float]

    Returns
    -------
    Classification
        With .category (1-4), .confidence (0.0-1.0), .reasoning, .evidence.
    """
    raw = solver_output.get("raw_status")
    if raw is None:
        return Classification(
            category=0,
            confidence=0.0,
            reasoning="raw_status missing; cannot classify.",
            evidence={"raw_status": None},
        )

    raw = int(raw)
    sqp_iter = solver_output.get("sqp_iter")
    has_constraint_violations = solver_output.get("has_constraint_violations", False)
    gnc_exec = solver_output.get("gnc_executability")

    # Normalize gnc_executability to a tri-state.
    gnc_state = _normalize_gnc(gnc_exec)

    # Evidence bundle for transparency.
    evidence: Dict[str, Any] = {
        "raw_status": raw,
        "raw_semantic": ACADOS_STATUS.get(raw, "UNKNOWN"),
        "sqp_iter": sqp_iter,
        "has_constraint_violations": has_constraint_violations,
        "gnc_executability": gnc_state,
    }

    # --- raw=0 (SUCCESS) path ---
    if raw == 0:
        return _classify_success(sqp_iter, has_constraint_violations,
                                 gnc_state, evidence, solver_output)

    # --- raw=2 (MAXITER) -- ambiguous: OCP infeasible vs QP infeasible ---
    if raw == 2:
        return _classify_maxiter(sqp_iter, has_constraint_violations,
                                 evidence, solver_output)

    # --- raw=3 (MINSTEP) -- linearized QP infeasible ---
    if raw == 3:
        return Classification(
            category=2,
            confidence=0.90,
            reasoning=(
                "acados returned MINSTEP (status 3): step computation failed "
                "due to an infeasible QP subproblem. This is a linearized QP "
                "infeasibility."
            ),
            evidence=evidence,
        )

    # --- raw=1 (NAN_DETECTED), raw=4 (QP_FAILURE), raw=5 (READY),
    #     raw=6 (UNBOUNDED), raw=7 (TIMEOUT) -- numerical non-convergence ---
    if raw in (1, 4, 5, 6, 7):
        return _classify_numerical(raw, sqp_iter, evidence, solver_output)

    # Unknown raw status.
    return Classification(
        category=0,
        confidence=0.0,
        reasoning=f"Unrecognized raw_status={raw}; outside acados enum range 0-7.",
        evidence=evidence,
    )


# ---------------------------------------------------------------------------
# Internal classification helpers
# ---------------------------------------------------------------------------

def _normalize_gnc(gnc_exec: Any) -> str:
    """Normalize gnc_executability to 'PASS', 'FAIL', 'OPEN', or 'UNKNOWN'."""
    if gnc_exec is None:
        return "UNKNOWN"
    if isinstance(gnc_exec, dict):
        status = gnc_exec.get("status", "").upper()
        if status in ("PASS", "CONVERGED", "EXECUTABLE"):
            return "PASS"
        if status in ("FAIL", "REJECTED", "NON_EXECUTABLE"):
            return "FAIL"
        return "OPEN"
    if isinstance(gnc_exec, str):
        upper = gnc_exec.upper()
        if upper in ("PASS", "CONVERGED", "EXECUTABLE"):
            return "PASS"
        if upper in ("FAIL", "REJECTED", "NON_EXECUTABLE"):
            return "FAIL"
        if upper == "OPEN":
            return "OPEN"
    return "UNKNOWN"


def _detect_constraint_violations_from_derivative_diag(
        derivative_diag: Optional[Dict[str, Any]]) -> bool:
    """Inspect derivative_diagnostics.json for post-solve constraint violations."""
    if derivative_diag is None:
        return False
    sol_res = derivative_diag.get("solution_constraint_residual")
    if sol_res is None:
        return False
    violation_count = sol_res.get("violation_count", 0)
    first_violation = sol_res.get("first_violation")
    return bool(violation_count > 0 and first_violation is not None)


def _classify_success(sqp_iter: Any, has_constraint_violations: bool,
                      gnc_state: str, evidence: Dict[str, Any],
                      solver_output: Dict[str, Any]) -> Classification:
    """Classify raw=0: converged, but may still produce non-executable output."""
    # Determine if output is non-executable from available signals.
    reasons: List[str] = []

    if has_constraint_violations:
        reasons.append("post-solve constraint violations detected")

    if gnc_state == "FAIL":
        reasons.append("GNC executability check FAILED")
    elif gnc_state == "OPEN":
        # OPEN means not yet reviewed; we can't definitively call it non-executable.
        # But if constraint violations exist, that is stronger evidence.
        pass

    if reasons:
        return Classification(
            category=4,
            confidence=_confidence_from_reasons(reasons, base=0.75),
            reasoning=(
                "Solver converged (ACADOS_SUCCESS) but trajectory fails "
                "post-solve checks: " + "; ".join(reasons) + "."
            ),
            evidence=evidence,
        )

    # Converged and no detected violations: this is a success, not a failure.
    # Still assign category 4 with low confidence if GNC is OPEN (unreviewed).
    if gnc_state == "OPEN":
        return Classification(
            category=4,
            confidence=0.30,
            reasoning=(
                "Solver converged but GNC executability review is still OPEN. "
                "Provisional Category 4 (low confidence) pending review."
            ),
            evidence=evidence,
        )

    # GNC passed: this is a true success, not a failure.
    return Classification(
        category=0,
        confidence=0.95,
        reasoning="Solver converged and all post-solve checks pass. No failure.",
        evidence=evidence,
    )


def _classify_maxiter(sqp_iter: Any, has_constraint_violations: bool,
                      evidence: Dict[str, Any],
                      solver_output: Dict[str, Any]) -> Classification:
    """Classify raw=2 (MAXITER): OCP infeasible vs QP infeasible."""
    if sqp_iter is None:
        # Without sqp_iter, default to OCP infeasible (Category 1) with
        # reduced confidence.
        return Classification(
            category=1,
            confidence=0.55,
            reasoning=(
                "acados returned MAXITER (status 2) but sqp_iter unavailable. "
                "Defaulting to Category 1 (OCP infeasible) with low confidence."
            ),
            evidence=evidence,
        )

    sqp_iter = int(sqp_iter)

    if sqp_iter < MAXITER_OCP_VS_QP_THRESHOLD:
        # Low iteration count: solver barely started. The OCP formulation is
        # likely fundamentally infeasible.
        confidence = 0.85
        if has_constraint_violations:
            confidence = 0.92  # corroborating evidence
        return Classification(
            category=1,
            confidence=confidence,
            reasoning=(
                f"acados MAXITER with sqp_iter={sqp_iter} "
                f"(< {MAXITER_OCP_VS_QP_THRESHOLD}). Solver made minimal "
                "progress, suggesting the OCP constraint set is fundamentally "
                "infeasible."
            ),
            evidence=evidence,
        )
    else:
        # High iteration count: SQP made progress but QP subproblems kept
        # failing. This is a QP infeasibility.
        qp_history = solver_output.get("qp_status_history", [])
        qp_fail_count = sum(1 for s in qp_history if s not in (0, 2))
        confidence = 0.80
        if qp_fail_count > 0:
            confidence = min(0.93, 0.80 + 0.02 * min(qp_fail_count, 6))
        return Classification(
            category=2,
            confidence=confidence,
            reasoning=(
                f"acados MAXITER with sqp_iter={sqp_iter} "
                f"(>= {MAXITER_OCP_VS_QP_THRESHOLD}). SQP iterated but hit "
                "iteration limit, indicating QP subproblems are struggling."
            ),
            evidence={**evidence,
                      "qp_failure_count": qp_fail_count if qp_history else None},
        )


def _classify_numerical(raw: int, sqp_iter: Any,
                        evidence: Dict[str, Any],
                        solver_output: Dict[str, Any]) -> Classification:
    """Classify numerical non-convergence failures (raw=1,4,5,6,7)."""
    semantic = ACADOS_STATUS.get(raw, "UNKNOWN")
    reasons: List[str] = []

    if raw == 1:
        reasons.append("NaN detected in solver iterates")
    elif raw == 4:
        qp_history = solver_output.get("qp_status_history", [])
        if qp_history:
            qp_fail_count = sum(1 for s in qp_history if s not in (0, 2))
            reasons.append(
                f"QP solver failure (qp_status_history has {qp_fail_count} "
                f"non-success entries)")
        else:
            reasons.append("QP solver failure in acados")
    elif raw == 5:
        reasons.append("solver returned READY (not a solved trajectory)")
    elif raw == 6:
        reasons.append("problem detected as unbounded")
    elif raw == 7:
        reasons.append("solver timed out")

    # Confidence base on how definitive the signal is.
    confidence_map = {1: 0.88, 4: 0.90, 5: 0.85, 6: 0.80, 7: 0.82}
    confidence = confidence_map.get(raw, 0.70)

    return Classification(
        category=3,
        confidence=confidence,
        reasoning=(
            f"acados returned {semantic} (status {raw}): "
            + "; ".join(reasons) + "."
        ),
        evidence=evidence,
    )


def _confidence_from_reasons(reasons: List[str], base: float = 0.70) -> float:
    """Compute confidence from number of corroborating reasons."""
    return min(0.98, base + 0.05 * len(reasons))


# ---------------------------------------------------------------------------
# Multi-source classification: collect evidence from diagnostic artifact files
# ---------------------------------------------------------------------------

def classify_from_files(
    solution_path: Optional[pathlib.Path] = None,
    verdict_path: Optional[pathlib.Path] = None,
    derivative_diag_path: Optional[pathlib.Path] = None,
    gnc_exec_path: Optional[pathlib.Path] = None,
    qp_statistics_path: Optional[pathlib.Path] = None,
) -> Classification:
    """Build a unified solver_output dict from diagnostic files and classify.

    At minimum, one of solution_path or verdict_path must be provided.
    Additional files enrich the evidence and increase confidence.
    """
    solver_output: Dict[str, Any] = {}

    # --- Load solution.json ---
    if solution_path is not None and solution_path.exists():
        sol = json.loads(solution_path.read_text(encoding="utf-8"))
        # run_phase0_case.py format
        _maybe_set(solver_output, "raw_status", sol.get("raw_status"))
        _maybe_set(solver_output, "sqp_iter", sol.get("sqp_iter"))
        _maybe_set(solver_output, "cost", sol.get("cost"))
        # phase2_ablation.py format (nested under "final_warm")
        fw = sol.get("final_warm")
        if isinstance(fw, dict):
            _maybe_set(solver_output, "raw_status", fw.get("raw_status"))
            _maybe_set(solver_output, "sqp_iter", fw.get("sqp_iter"))
            _maybe_set(solver_output, "cost", fw.get("cost"))
            _maybe_set(solver_output, "nlp_residuals", fw.get("nlp_residuals"))
            _maybe_set(solver_output, "qp_status_history",
                       fw.get("qp_status_history"))
            _maybe_set(solver_output, "qp_iter_history",
                       fw.get("qp_iter_history"))
            _maybe_set(solver_output, "raw_semantic", fw.get("raw_semantic"))

    # --- Load verdict.json ---
    if verdict_path is not None and verdict_path.exists():
        verdict = json.loads(verdict_path.read_text(encoding="utf-8"))
        _maybe_set(solver_output, "raw_status", verdict.get("raw_status"))
        _maybe_set(solver_output, "gnc_executability",
                   verdict.get("gnc_executability"))

    # --- Load qp_statistics.json ---
    if qp_statistics_path is not None and qp_statistics_path.exists():
        qps = json.loads(qp_statistics_path.read_text(encoding="utf-8"))
        _maybe_set(solver_output, "raw_status", qps.get("raw_nlp_status"))
        _maybe_set(solver_output, "sqp_iter", qps.get("sqp_iter"))
        _maybe_set(solver_output, "nlp_residuals",
                   qps.get("nlp_residuals_stat_eq_ineq_comp"))
        _maybe_set(solver_output, "qp_status_history",
                   qps.get("qp_status_history"))
        _maybe_set(solver_output, "qp_iter_history",
                   qps.get("qp_iter_history"))
        _maybe_set(solver_output, "raw_semantic",
                   qps.get("raw_nlp_status_semantic"))

    # --- Load derivative diagnostics (for constraint violations) ---
    if derivative_diag_path is not None and derivative_diag_path.exists():
        diag = json.loads(derivative_diag_path.read_text(encoding="utf-8"))
        has_cv = _detect_constraint_violations_from_derivative_diag(diag)
        if has_cv:
            _maybe_set(solver_output, "has_constraint_violations", True)

    # --- Load gnc_executability.json ---
    if gnc_exec_path is not None and gnc_exec_path.exists():
        gnc = json.loads(gnc_exec_path.read_text(encoding="utf-8"))
        if solver_output.get("gnc_executability") is None:
            solver_output["gnc_executability"] = gnc

    return classify(solver_output)


# ---------------------------------------------------------------------------
# Enrichment: add failure_classification to an existing verdict dict
# ---------------------------------------------------------------------------

def enrich_verdict(verdict: Dict[str, Any],
                   solver_output: Optional[Dict[str, Any]] = None,
                   derivative_diag: Optional[Dict[str, Any]] = None,
                   gnc_exec: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """Add a 'failure_classification' field to a verdict dict.

    Returns the modified verdict (also mutates in-place).
    """
    so: Dict[str, Any] = dict(solver_output) if solver_output else {}

    # Pull signals from verdict itself.
    _maybe_set(so, "raw_status", verdict.get("raw_status"))
    _maybe_set(so, "gnc_executability", verdict.get("gnc_executability"))

    # Pull constraint violations from derivative_diag if available.
    if derivative_diag is not None:
        has_cv = _detect_constraint_violations_from_derivative_diag(
            derivative_diag)
        if has_cv:
            _maybe_set(so, "has_constraint_violations", True)

    # Pull GNC from explicit gnc_exec if available.
    if gnc_exec is not None:
        _maybe_set(so, "gnc_executability", gnc_exec)

    classification = classify(so)
    verdict["failure_classification"] = dict(classification)
    return verdict


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="X4 Failure Classifier -- classify solver diagnostic output"
    )
    parser.add_argument("--solution", type=pathlib.Path,
                        help="Path to solution.json")
    parser.add_argument("--verdict", type=pathlib.Path,
                        help="Path to verdict.json")
    parser.add_argument("--derivative-diag", type=pathlib.Path,
                        help="Path to derivative_diagnostics.json")
    parser.add_argument("--gnc-exec", type=pathlib.Path,
                        help="Path to gnc_executability.json")
    parser.add_argument("--qp-statistics", type=pathlib.Path,
                        help="Path to qp_statistics.json")
    parser.add_argument("--enrich", action="store_true",
                        help="Modify verdict.json in-place by adding "
                             "failure_classification field")
    parser.add_argument("--output", type=pathlib.Path, default=None,
                        help="Write classification JSON to file (- for stdout)")
    args = parser.parse_args()

    if args.solution is None and args.verdict is None:
        parser.error("At least one of --solution or --verdict is required")

    # Build solver_output from all available files.
    solver_output: Dict[str, Any] = {}

    for path, key_fn in [
        (args.solution, _extract_from_solution),
        (args.verdict, _extract_from_verdict),
        (args.qp_statistics, _extract_from_qp_statistics),
    ]:
        if path is not None and path.exists():
            key_fn(path, solver_output)

    # Constraint violations from derivative diagnostics.
    if args.derivative_diag is not None and args.derivative_diag.exists():
        diag = json.loads(args.derivative_diag.read_text(encoding="utf-8"))
        solver_output["has_constraint_violations"] = (
            _detect_constraint_violations_from_derivative_diag(diag))

    # GNC executability.
    if args.gnc_exec is not None and args.gnc_exec.exists():
        gnc = json.loads(args.gnc_exec.read_text(encoding="utf-8"))
        solver_output.setdefault("gnc_executability", gnc)

    classification = classify(solver_output)
    result = dict(classification)
    result["input_summary"] = {
        "raw_status": solver_output.get("raw_status"),
        "sqp_iter": solver_output.get("sqp_iter"),
        "has_constraint_violations": solver_output.get(
            "has_constraint_violations", False),
        "gnc_executability": solver_output.get("gnc_executability"),
    }

    # Enrich verdict in-place if requested.
    if args.enrich and args.verdict is not None:
        verdict = json.loads(args.verdict.read_text(encoding="utf-8"))
        verdict["failure_classification"] = {
            "category": classification["category"],
            "category_name": classification["category_name"],
            "confidence": classification["confidence"],
            "reasoning": classification["reasoning"],
        }
        args.verdict.write_text(
            json.dumps(verdict, indent=2, sort_keys=True, allow_nan=False)
            + "\n", encoding="utf-8")
        print(f"Enriched {args.verdict} with failure_classification")

    # Write output.
    output_text = json.dumps(result, indent=2, sort_keys=True,
                             allow_nan=False) + "\n"
    if args.output is None or args.output == pathlib.Path("-"):
        print(output_text)
    else:
        args.output.write_text(output_text, encoding="utf-8")
    return 0


def _maybe_set(output: Dict[str, Any], key: str, value: Any) -> None:
    """Set key in output only if value is not None and key is either absent
    or currently None."""
    if value is not None and output.get(key) is None:
        output[key] = value


def _extract_from_solution(path: pathlib.Path,
                           output: Dict[str, Any]) -> None:
    sol = json.loads(path.read_text(encoding="utf-8"))
    _maybe_set(output, "raw_status", sol.get("raw_status"))
    _maybe_set(output, "sqp_iter", sol.get("sqp_iter"))
    _maybe_set(output, "cost", sol.get("cost"))
    fw = sol.get("final_warm")
    if isinstance(fw, dict):
        _maybe_set(output, "raw_status", fw.get("raw_status"))
        _maybe_set(output, "sqp_iter", fw.get("sqp_iter"))
        _maybe_set(output, "cost", fw.get("cost"))
        _maybe_set(output, "nlp_residuals", fw.get("nlp_residuals"))
        _maybe_set(output, "qp_status_history", fw.get("qp_status_history"))
        _maybe_set(output, "qp_iter_history", fw.get("qp_iter_history"))
        _maybe_set(output, "raw_semantic", fw.get("raw_semantic"))


def _extract_from_verdict(path: pathlib.Path,
                          output: Dict[str, Any]) -> None:
    verdict = json.loads(path.read_text(encoding="utf-8"))
    _maybe_set(output, "raw_status", verdict.get("raw_status"))
    _maybe_set(output, "gnc_executability", verdict.get("gnc_executability"))


def _extract_from_qp_statistics(path: pathlib.Path,
                                output: Dict[str, Any]) -> None:
    qps = json.loads(path.read_text(encoding="utf-8"))
    _maybe_set(output, "raw_status", qps.get("raw_nlp_status"))
    _maybe_set(output, "sqp_iter", qps.get("sqp_iter"))
    _maybe_set(output, "nlp_residuals",
               qps.get("nlp_residuals_stat_eq_ineq_comp"))
    _maybe_set(output, "qp_status_history", qps.get("qp_status_history"))
    _maybe_set(output, "qp_iter_history", qps.get("qp_iter_history"))
    _maybe_set(output, "raw_semantic", qps.get("raw_nlp_status_semantic"))


if __name__ == "__main__":
    sys.exit(main())
