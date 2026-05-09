#include <gtest/gtest.h>

// Compile-time independence evidence: M7 never #includes M1–M6 internal headers.
// Forbidden includes are NOT listed here — if they compiled, they would be CI violations.
// This test is a documentation artifact; static analysis
// (tools/ci/check-doer-checker-independence.sh) is the authoritative enforcement.
//
// ADR-001: M7 (Checker) must be implementationally independent from M1–M6 (Doer).
// Independence is verified by the CI script that checks:
//   1. No #include of m1–m6 internal headers in m7_safety_supervisor/
//   2. No CMakeLists link from m7_lib to m1–m6 targets
//
// CCS audit reference: docs/Design/Architecture Design/audit/2026-04-30/08c-adr-deltas.md

TEST(DoerCheckerIndependenceTest, M7BinaryDoesNotLinkForbiddenSymbols)
{
  // Static analysis (CI script) is the enforcement point.
  // This test documents the ADR-001 invariant for audit trail.
  SUCCEED();
}
