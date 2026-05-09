#!/usr/bin/env bash
# tools/ci/check-rl-isolation.sh
# RL Isolation L1 Repo Boundary Check (per architecture v1.1.3-pre-stub §F.4 +
# spec docs/Design/Detailed Design/D1.3a-simulator/01-spec.md §v3.1.5).
#
# Verifies cross-group imports/includes between:
#   src/l3_tdl_kernel/  (frozen, MISRA C++:2023, DNV-RP-0513 assured)
#   src/sim_workbench/  (Python sim tools / D1.3a-b)
#   src/rl_workbench/   (Phase 4 启动；目前 .gitkeep + README only)
#
# Bidirectional violation detection:
#   - sim_workbench/rl_workbench MUST NOT import/include kernel packages
#   - l3_tdl_kernel MUST NOT import/include sim/rl packages
#
# Allowlist: same-line comment "# rl-isolation-ok: <reason>" (Python)
#         or "// rl-isolation-ok: <reason>" (C++) — reason MUST be non-empty.

set -euo pipefail

if (( BASH_VERSINFO[0] < 4 )); then
    echo "FATAL: requires bash >= 4.0 (got ${BASH_VERSION}). On macOS: 'brew install bash'." >&2
    exit 2
fi

# Sanity: required directories exist (post T2 git mv).
for d in src/l3_tdl_kernel src/sim_workbench src/rl_workbench; do
    if [[ ! -d "$d" ]]; then
        echo "FATAL: required directory '$d' not found." >&2
        echo "       T2 (git mv) may not have landed; investigate before retrying." >&2
        exit 2
    fi
done

errors=0

# Patterns
readonly PY_KERNEL_RE='^[[:space:]]*(from|import)[[:space:]]+(m[1-8]_[a-z_]+|l3_msgs|l3_external_msgs|common)([[:space:]]|\.|$)'
readonly PY_SIMRL_RE='^[[:space:]]*(from|import)[[:space:]]+(sim_workbench|rl_workbench|fcb_simulator|ship_sim_interfaces|sil_mock_publisher|l3_external_mock_publisher|ais_bridge)([[:space:]]|\.|$)'
readonly CPP_KERNEL_RE='^[[:space:]]*#include[[:space:]]*[<"](m[1-8]_[a-z_]+|l3_msgs|l3_external_msgs|common)/'
readonly CPP_SIMRL_RE='^[[:space:]]*#include[[:space:]]*[<"](sim_workbench|rl_workbench|fcb_simulator|ship_sim_interfaces|sil_mock_publisher|l3_external_mock_publisher|ais_bridge)/'

scan() {
    local label="$1"; local pattern="$2"; local comment_marker="$3"; shift 3
    local -a search_dirs=("$@")
    local includes=()
    if [[ "$comment_marker" == "#" ]]; then
        includes=(--include='*.py')
    else
        includes=(--include='*.hpp' --include='*.cpp' --include='*.h' --include='*.cc')
    fi
    local out=""
    if out=$(grep -rEn "$pattern" "${search_dirs[@]}" "${includes[@]}" 2>/dev/null | grep -v "${comment_marker} rl-isolation-ok:" 2>/dev/null); then
        :
    fi
    if [[ -n "$out" ]]; then
        echo "==== VIOLATION (${label}) ===="
        echo "$out"
        local count
        count=$(printf '%s\n' "$out" | wc -l | tr -d ' ')
        errors=$((errors + count))
    fi
}

scan "python: kernel-import-from-sim/rl" "$PY_KERNEL_RE"   "#"  src/sim_workbench src/rl_workbench
scan "python: sim/rl-import-from-kernel" "$PY_SIMRL_RE"    "#"  src/l3_tdl_kernel
scan "cpp:    kernel-include-from-sim/rl" "$CPP_KERNEL_RE" "//" src/sim_workbench src/rl_workbench
scan "cpp:    sim/rl-include-from-kernel" "$CPP_SIMRL_RE"  "//" src/l3_tdl_kernel

if [[ $errors -gt 0 ]]; then
    echo ""
    echo "FAIL: ${errors} RL-isolation violation(s) detected." >&2
    echo "      Architecture v1.1.3-pre-stub §F.4 requires three-group separation:" >&2
    echo "        l3_tdl_kernel × sim_workbench × rl_workbench" >&2
    echo "      If a cross-group reference is intentional, add same-line marker:" >&2
    echo "         '# rl-isolation-ok: <non-empty reason>' (Python)" >&2
    echo "         '// rl-isolation-ok: <non-empty reason>' (C++)" >&2
    exit 1
fi

echo "PASS: RL-isolation check clean (kernel × sim_workbench × rl_workbench)."
exit 0
