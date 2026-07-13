#!/usr/bin/env bash
# ============================================================================
# A4000 SIL acceptance — one-shot deterministic RTF + full multi-screen E2E.
#
# Run from the A4000 checkout under test, AFTER services are up
# (`source scripts/a4000-env.sh && npm run sys:start`):
#
#     cd /home/marine.huang/Code/mass-l3/.worktrees/l3-tdl
#     source scripts/a4000-env.sh
#     ./scripts/a4000-acceptance.sh
#
# Two gating stages:
#   [2] RTF / determinism   headless sweep {1,5,10}x == nominal (>=85% eff)
#   [3] full multi-screen    the real 3-screen Playwright test passing end-to-end:
#                            A_rtf (HMI-path 10x in band) + A_turn (real rule14
#                            avoidance turn ≥20°) + A_recon (HMI store == backend).
#
# A_turn went green on 2026-06-03 once M5 was unblocked (casadi ipopt plugin
# build fix): the avoidance chain M2→M4→M5→bridge→actuator now actually turns
# the ship (scored safety=1.0/compliance=1.0). Exit code = both stages green.
# ============================================================================
set -uo pipefail
cd "$(dirname "$0")/.."

SCENARIO="${SCENARIO:-colreg-rule14-ho}"
: "${ORCH_URL:=https://127.0.0.1:18000}"
: "${ORCH_PORT:=18000}"
: "${FOX_PORT:=18765}"
export ORCH_URL ORCH_PORT FOX_PORT
RATES_HEADLESS="${RATES_HEADLESS:-1 5 10}"
EFF_MIN=85                            # headless efficiency floor (%)
GATE_FAIL=0

G='\033[32m'; R='\033[31m'; Y='\033[33m'; B='\033[1m'; N='\033[0m'
ok(){ echo -e "  ${G}PASS${N} $*"; }
bad(){ echo -e "  ${R}FAIL${N} $*"; GATE_FAIL=1; }
note(){ echo -e "  ${Y}NOTE${N} $*"; }

# Ensure npx is reachable (shared account: Node 20 lives under nvm, no global default)
if ! command -v npx >/dev/null 2>&1; then
  export NVM_DIR="$HOME/.nvm"; [ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"
  nvm use 20 >/dev/null 2>&1 || true
fi

# This host is the development and validation host. Acceptance never updates
# source state; integrate through task branches/worktrees before running it.
if [[ "${1:-}" == "--sync" ]]; then
  echo -e "${R}${B}ERROR: --sync is disabled; merge through git worktrees before acceptance${N}" >&2
  exit 2
fi

# ---- Stage 1: orchestrator health ----------------------------------------
echo -e "${B}[1] orchestrator health (${ORCH_URL})${N}"
if curl -sk --max-time 10 "${ORCH_URL}/api/v1/health" | grep -q '"status":"ok"'; then
  ok "orchestrator REST ok"
else
  bad "orchestrator /health not ok — is the stack up? (npm run sys:start)"
  echo -e "\n${R}${B}ABORT: backend not reachable${N}"; exit 1
fi

# ---- Stage 2: deterministic headless RTF sweep (GATING) -------------------
echo -e "${B}[2] headless RTF sweep {${RATES_HEADLESS}} (deterministic, no browser)${N}"
for R in $RATES_HEADLESS; do
  # window tuned so settle+sample fits inside the ~295s/R wall budget
  if   (( R <= 1 )); then S=4; W=25; I=1.0
  elif (( R <= 5 )); then S=4; W=20; I=1.0
  else                    S=4; W=18; I=0.5; fi
  line=$(SETTLE_S=$S SAMPLE_S=$W INTERVAL=$I \
         python3 scripts/rtf_headless_sweep.py "$SCENARIO" "$R" 2>&1 \
         | grep -E "nominal ->" | tail -1)
  # line e.g.:  "    10x nominal -> 10.00x real  (100%)"
  meas=$(echo "$line" | grep -oE '[0-9]+\.[0-9]+x real' | grep -oE '[0-9]+\.[0-9]+')
  pct=$(echo "$line"  | grep -oE '\([0-9]+%\)'         | grep -oE '[0-9]+')
  if [[ -n "$meas" && -n "$pct" ]] && (( pct >= EFF_MIN )); then
    ok "${R}x -> ${meas}x  (eff ${pct}%)"
  else
    bad "${R}x -> '${line:-no output}'  (eff floor ${EFF_MIN}%)"
  fi
done

# ---- Stage 3: full multi-screen Playwright @10x ---------------------------
echo -e "${B}[3] full multi-screen Playwright @10x (real 3-screen UI: RTF + avoidance + recon)${N}"
LOG=$(mktemp)
( cd web && RATE=10 ORCH_PORT="$ORCH_PORT" FOX_PORT="$FOX_PORT" \
    timeout 260 npx playwright test e2e/mvp_consistency.spec.ts \
      -g "MVP consistency" --retries=0 --reporter=line >"$LOG" 2>&1 )
pw_rc=$?

# Echo whichever per-assertion lines flushed (A_turn's console.log can be
# swallowed by the line reporter on failure; the exit code is authoritative).
grep -E "^A_rtf:|^A_turn:|^A_recon:" "$LOG" | sed 's/^/    /'
if [[ $pw_rc -eq 0 ]]; then
  ok "multi-screen test PASSED — A_rtf in band + A_turn real avoidance(≥20°) + A_recon HMI==backend"
else
  bad "multi-screen test FAILED (rc=$pw_rc)"
  echo "    --- last 25 lines of Playwright log ---"; tail -25 "$LOG" | sed 's/^/    /'
fi
rm -f "$LOG"

# ---- Verdict --------------------------------------------------------------
echo
if (( GATE_FAIL == 0 )); then
  echo -e "${G}${B}ACCEPTANCE PASS${N} — RTF deterministic {${RATES_HEADLESS}}x + full multi-screen E2E green (RTF+avoidance+recon)."
  exit 0
else
  echo -e "${R}${B}ACCEPTANCE FAIL${N} — a gating check failed (see above)."
  exit 1
fi
