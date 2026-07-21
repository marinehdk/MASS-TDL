#!/usr/bin/env python3
"""Unit tests for X4 Failure Classifier.

Covers all four failure categories plus edge cases.
Run:
    python3 -m pytest test_x4_failure_classifier.py -v
    python3 test_x4_failure_classifier.py
"""

from __future__ import annotations

import json
import sys
import unittest

# Ensure the diagnostics directory is on sys.path.
sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent))

import x4_failure_classifier as x4


# ---------------------------------------------------------------------------
# Test helpers
# ---------------------------------------------------------------------------

def _so(raw_status, sqp_iter=None, **kwargs):
    """Build a minimal solver_output dict."""
    d = {"raw_status": raw_status}
    if sqp_iter is not None:
        d["sqp_iter"] = sqp_iter
    d.update(kwargs)
    return d


# ---------------------------------------------------------------------------
# Category 1: Original OCP Infeasible
# ---------------------------------------------------------------------------

class TestCategory1_OCPInfeasible(unittest.TestCase):
    """raw=2 (MAXITER) with low sqp_iter -> Category 1."""

    def test_maxiter_low_sqp_iter_basic(self):
        result = x4.classify(_so(raw_status=2, sqp_iter=3))
        self.assertEqual(result["category"], 1)
        self.assertIn("OCP", result["category_name"])
        self.assertGreater(result["confidence"], 0.7)
        self.assertIn("sqp_iter=3", result["reasoning"])

    def test_maxiter_low_sqp_iter_with_constraint_violations(self):
        result = x4.classify(_so(raw_status=2, sqp_iter=5,
                                 has_constraint_violations=True))
        self.assertEqual(result["category"], 1)
        self.assertGreaterEqual(result["confidence"], 0.90)

    def test_maxiter_low_sqp_iter_boundary(self):
        """sqp_iter=9: just below threshold; still Category 1."""
        result = x4.classify(_so(raw_status=2, sqp_iter=9))
        self.assertEqual(result["category"], 1)

    def test_maxiter_no_sqp_iter_defaults_to_cat1(self):
        """Missing sqp_iter: default to Category 1 with reduced confidence."""
        result = x4.classify(_so(raw_status=2))
        self.assertEqual(result["category"], 1)
        self.assertLess(result["confidence"], 0.65)


# ---------------------------------------------------------------------------
# Category 2: Linearized QP Infeasible
# ---------------------------------------------------------------------------

class TestCategory2_QPInfeasible(unittest.TestCase):
    """raw=3 (MINSTEP) or raw=2 with high sqp_iter -> Category 2."""

    def test_minstep_basic(self):
        result = x4.classify(_so(raw_status=3))
        self.assertEqual(result["category"], 2)
        self.assertIn("QP", result["category_name"])
        self.assertGreater(result["confidence"], 0.85)

    def test_maxiter_high_sqp_iter(self):
        result = x4.classify(_so(raw_status=2, sqp_iter=20))
        self.assertEqual(result["category"], 2)
        self.assertGreater(result["confidence"], 0.75)

    def test_maxiter_high_sqp_iter_with_qp_history(self):
        result = x4.classify(_so(raw_status=2, sqp_iter=15,
                                 qp_status_history=[0, 3, 0, 3, 3]))
        self.assertEqual(result["category"], 2)
        self.assertGreater(result["confidence"], 0.85)
        self.assertEqual(result["evidence"].get("qp_failure_count"), 3)

    def test_minstep_with_qp_history(self):
        result = x4.classify(_so(raw_status=3,
                                 qp_status_history=[0, 0, 3]))
        self.assertEqual(result["category"], 2)


# ---------------------------------------------------------------------------
# Category 3: Numerical Non-Convergence
# ---------------------------------------------------------------------------

class TestCategory3_NumericalNonConvergence(unittest.TestCase):
    """raw=1,4,5,6,7 -> Category 3."""

    def test_nan_detected(self):
        result = x4.classify(_so(raw_status=1))
        self.assertEqual(result["category"], 3)
        self.assertIn("NaN", result["reasoning"])

    def test_qp_failure(self):
        result = x4.classify(_so(raw_status=4))
        self.assertEqual(result["category"], 3)
        self.assertIn("QP", result["reasoning"])

    def test_qp_failure_with_status_history(self):
        result = x4.classify(_so(raw_status=4,
                                 qp_status_history=[0, 3, 3]))
        self.assertEqual(result["category"], 3)
        self.assertIn("2 non-success", result["reasoning"])

    def test_ready_status(self):
        result = x4.classify(_so(raw_status=5))
        self.assertEqual(result["category"], 3)

    def test_unbounded(self):
        result = x4.classify(_so(raw_status=6))
        self.assertEqual(result["category"], 3)

    def test_timeout(self):
        result = x4.classify(_so(raw_status=7))
        self.assertEqual(result["category"], 3)


# ---------------------------------------------------------------------------
# Category 4: Output Non-Executable
# ---------------------------------------------------------------------------

class TestCategory4_OutputNonExecutable(unittest.TestCase):
    """raw=0 but tail-gate / constraint violations -> Category 4."""

    def test_converged_with_constraint_violations(self):
        result = x4.classify(_so(raw_status=0, sqp_iter=50,
                                 has_constraint_violations=True))
        self.assertEqual(result["category"], 4)
        self.assertIn("constraint violations", result["reasoning"])
        self.assertGreater(result["confidence"], 0.70)

    def test_converged_with_gnc_fail(self):
        result = x4.classify(_so(raw_status=0, sqp_iter=30,
                                 gnc_executability="FAIL"))
        self.assertEqual(result["category"], 4)
        self.assertIn("GNC", result["reasoning"])

    def test_converged_with_gnc_fail_dict(self):
        result = x4.classify(_so(raw_status=0, sqp_iter=30,
                                 gnc_executability={"status": "FAIL"}))
        self.assertEqual(result["category"], 4)

    def test_converged_with_both_violations_and_gnc_fail(self):
        result = x4.classify(_so(raw_status=0, sqp_iter=25,
                                 has_constraint_violations=True,
                                 gnc_executability="FAIL"))
        self.assertEqual(result["category"], 4)
        self.assertGreater(result["confidence"], 0.80)

    def test_converged_gnc_open_no_violations(self):
        """GNC still open: provisional Category 4 with low confidence."""
        result = x4.classify(_so(raw_status=0, sqp_iter=40,
                                 gnc_executability="OPEN"))
        self.assertEqual(result["category"], 4)
        self.assertLess(result["confidence"], 0.40)

    def test_converged_gnc_pass_no_violations(self):
        """GNC passed: no failure (category 0)."""
        result = x4.classify(_so(raw_status=0, sqp_iter=40,
                                 gnc_executability="PASS"))
        self.assertEqual(result["category"], 0)
        self.assertGreater(result["confidence"], 0.90)


# ---------------------------------------------------------------------------
# Edge cases
# ---------------------------------------------------------------------------

class TestEdgeCases(unittest.TestCase):
    """Edge cases and boundary conditions."""

    def test_missing_raw_status(self):
        result = x4.classify({})
        self.assertEqual(result["category"], 0)
        self.assertEqual(result["confidence"], 0.0)

    def test_unknown_raw_status(self):
        result = x4.classify(_so(raw_status=99))
        self.assertEqual(result["category"], 0)

    def test_deterministic_same_input(self):
        """Same input must produce same output (deterministic)."""
        r1 = x4.classify(_so(raw_status=2, sqp_iter=3,
                             has_constraint_violations=True))
        r2 = x4.classify(_so(raw_status=2, sqp_iter=3,
                             has_constraint_violations=True))
        self.assertEqual(r1["category"], r2["category"])
        self.assertEqual(r1["confidence"], r2["confidence"])
        self.assertEqual(r1["reasoning"], r2["reasoning"])

    def test_category_0_is_not_a_failure(self):
        """Category 0 indicates no failure detected (success case)."""
        result = x4.classify(_so(raw_status=0, sqp_iter=40,
                                 gnc_executability="PASS"))
        self.assertEqual(result["category"], 0)
        # The reasoning should indicate convergence / pass, not a failure category.
        self.assertIn("converged", result["reasoning"].lower())
        self.assertNotIn("Category 1", result["reasoning"])
        self.assertNotIn("Category 2", result["reasoning"])
        self.assertNotIn("Category 3", result["reasoning"])
        self.assertNotIn("Category 4", result["reasoning"])

    def test_classification_attribute_access(self):
        result = x4.classify(_so(raw_status=3))
        self.assertEqual(result.category, 2)
        self.assertEqual(result["category"], 2)
        self.assertTrue(isinstance(result, dict))

    def test_all_acados_status_codes_classified(self):
        """Every known acados status code produces a non-zero category or
        category-0 (success)."""
        for raw in range(8):
            result = x4.classify(_so(raw_status=raw, sqp_iter=5))
            self.assertIn(result["category"], range(5),
                          f"raw={raw} produced category={result['category']}")

    def test_gnc_normalization_variants(self):
        """GNC executability normalization handles various formats."""
        self.assertEqual(x4._normalize_gnc(None), "UNKNOWN")
        self.assertEqual(x4._normalize_gnc("PASS"), "PASS")
        self.assertEqual(x4._normalize_gnc("FAIL"), "FAIL")
        self.assertEqual(x4._normalize_gnc("OPEN"), "OPEN")
        self.assertEqual(x4._normalize_gnc("converged"), "PASS")
        self.assertEqual(x4._normalize_gnc("rejected"), "FAIL")
        self.assertEqual(x4._normalize_gnc({"status": "PASS"}), "PASS")
        self.assertEqual(x4._normalize_gnc({"status": "FAIL"}), "FAIL")
        self.assertEqual(x4._normalize_gnc({"status": "OPEN"}), "OPEN")
        self.assertEqual(x4._normalize_gnc("random"), "UNKNOWN")


# ---------------------------------------------------------------------------
# File-based classification tests
# ---------------------------------------------------------------------------

class TestClassifyFromFiles(unittest.TestCase):
    """Integration tests using real diagnostic files from runs/m5_solver_diag."""

    @classmethod
    def setUpClass(cls):
        # Walk up from the test file directory to find the git worktree root
        # (the directory containing ".git").
        p = __import__("pathlib").Path(__file__).resolve().parent
        worktree_root = p
        for _ in range(12):
            if (worktree_root / ".git").exists():
                break
            worktree_root = worktree_root.parent
        cls.diag_root = (
            worktree_root / "runs" / "m5_solver_diag"
            / "4fd37fd7e9fc435656e2154d92b859920a0eb646"
            / "phase2" / "qp_ric_alg_only" / "target2500_exact"
        )

    def test_classify_from_solution_file(self):
        sol_path = self.diag_root / "solution.json"
        if not sol_path.exists():
            self.skipTest("Diagnostic solution.json not found")
        result = x4.classify_from_files(solution_path=sol_path)
        # raw_status=4 (QP_FAILURE) -> Category 3
        self.assertEqual(result["category"], 3)
        self.assertGreaterEqual(result["confidence"], 0.80)

    def test_classify_from_all_files(self):
        paths = {
            "solution_path": self.diag_root / "solution.json",
            "verdict_path": self.diag_root / "verdict.json",
            "derivative_diag_path": self.diag_root / "derivative_diagnostics.json",
            "gnc_exec_path": self.diag_root / "gnc_executability.json",
            "qp_statistics_path": self.diag_root / "qp_statistics.json",
        }
        if not paths["solution_path"].exists():
            self.skipTest("Diagnostic suite not found")
        result = x4.classify_from_files(**{k: v for k, v in paths.items()
                                            if v.exists()})
        # With solution alone we get Category 3; full files may have additional
        # signals. Verify result is well-formed.
        self.assertIn(result["category"], range(5))
        self.assertIsInstance(result["confidence"], float)
        self.assertIsInstance(result["reasoning"], str)
        self.assertIsInstance(result["evidence"], dict)

    def test_enrich_verdict(self):
        verdict_path = self.diag_root / "verdict.json"
        if not verdict_path.exists():
            self.skipTest("Diagnostic verdict.json not found")
        verdict = json.loads(verdict_path.read_text(encoding="utf-8"))
        enriched = x4.enrich_verdict(verdict)
        self.assertIn("failure_classification", enriched)
        fc = enriched["failure_classification"]
        self.assertIn("category", fc)
        self.assertIn("confidence", fc)
        self.assertIn("reasoning", fc)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    unittest.main(verbosity=2)
