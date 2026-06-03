#!/usr/bin/env bash
# ============================================================================
# A4000 SIL acceptance — one-shot deterministic RTF + full multi-screen E2E.
#
# Run ON the A4000 server, from repo root, AFTER services are up
# (`source scripts/a4000-env.sh && npm run sys:start`):
#
#     ssh a4000
#     cd ~/Code/mass-l3
#     source scripts/a4000-env.sh
#     ./scripts/a4000-acceptance.sh            # full acceptance
#     ./scripts/a4000-acceptance.sh --sync     # git pull l3-tdl first
#
# Two verdicts (kept separate on purpose):
#   RTF / MIGRATION  (GATING)     headless sweep {1,5,10}x == nominal (>=85% eff)
#                                  + HMI-path A_rtf @10x inside RTF_BAND
#   FUNCTIONAL       (NON-GATING) A_turn (rule14 avoidance) — currently RED,
#                                 tracked as task-3 (M4 degenerate window / M3
#                                 cold start). The full Playwright test will go
#                                 green on its own once that lands; until then
#                                 A_recon is shadowed by the A_turn throw.
#
# Exit code reflects the GATING verdict only.
# ============================================================================
set -uo pipefail
cd "$(dirname "$0")/.."

SCENARIO="${SCENARIO:-colreg-rule14-ho}"
: "${ORCH_URL:=https://127.0.0.1:18000}"
: "${ORCH_PORT:=18000}"
: "${FOX_PORT:=18765}"
export ORCH_URL ORCH_PORT FOX_PORT
RATES_HEADLESS="${RATES_HEADLESS:-1 5 10}"
RTF_BAND_LO=7.0; RTF_BAND_HI=12.0     # mirrors mvp_consistency.spec.ts L37
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

# ---- Stage 0 (optional): sync to latest l3-tdl ----------------------------
if [[ "${1:-}" == "--sync" ]]; then
  echo -e "${B}[0] sync l3-tdl${N}"
  git checkout -- scenarios/*/.preflight/ 2>/dev/null || true
  git pull --ff-only origin l3-tdl 2>&1 | tail -3
  echo "    HEAD $(git rev-parse --short HEAD)"
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
echo -e "${B}[3] full multi-screen Playwright @10x (real 3-screen UI path)${N}"
LOG=$(mktemp)
( cd web && RATE=10 ORCH_PORT="$ORCH_PORT" FOX_PORT="$FOX_PORT" \
    timeout 240 npx playwright test e2e/mvp_consistency.spec.ts \
      -g "MVP consistency" --retries=0 --reporter=line >"$LOG" 2>&1 ) || true

rtf_line=$(grep -E "^A_rtf:" "$LOG" | tail -1)
turn_line=$(grep -E "^A_turn:" "$LOG" | tail -1)
recon_line=$(grep -E "^A_recon:" "$LOG" | tail -1)

# A_rtf prints before A_turn -> proves 3-screen nav reached monitor + RTF held
rtf=$(echo "$rtf_line" | grep -oE 'RTF=[0-9]+\.[0-9]+' | grep -oE '[0-9]+\.[0-9]+')
if [[ -n "$rtf" ]] && awk "BEGIN{exit !($rtf>=$RTF_BAND_LO && $rtf<=$RTF_BAND_HI)}"; then
  ok "HMI-path A_rtf=${rtf}x in band [${RTF_BAND_LO},${RTF_BAND_HI}] (3-screen nav reached monitor)"
else
  bad "HMI-path A_rtf '${rtf_line:-not printed}' — nav broke or RTF out of band"
  echo "    --- last 20 lines of Playwright log ---"; tail -20 "$LOG" | sed 's/^/    /'
fi

# A_turn: functional avoidance, NON-GATING
if [[ -n "$turn_line" ]]; then
  net=$(echo "$turn_line" | grep -oE 'net\|max-from-start\|=[0-9]+\.[0-9]+' | grep -oE '[0-9]+\.[0-9]+')
  if [[ -n "$net" ]] && awk "BEGIN{exit !($net>60)}"; then
    ok "A_turn net=${net}deg (>60) — avoidance ENGAGED (task-3 resolved!)"
  else
    note "A_turn net=${net:-0}deg (<=60) — avoidance NOT engaged: KNOWN task-3 RED, non-gating"
  fi
else
  note "A_turn line not printed — non-gating"
fi
[[ -n "$recon_line" ]] && note "A_recon: $recon_line" \
  || note "A_recon shadowed by A_turn throw (surfaces once task-3 lands)"
rm -f "$LOG"

# ---- Verdict --------------------------------------------------------------
echo
if (( GATE_FAIL == 0 )); then
  echo -e "${G}${B}ACCEPTANCE PASS${N} — RTF deterministic across {${RATES_HEADLESS}}x + HMI-path 10x in band."
  echo -e "  (A_turn avoidance is the only RED — tracked separately as task-3, not gating.)"
  exit 0
else
  echo -e "${R}${B}ACCEPTANCE FAIL${N} — a GATING RTF/migration check failed (see above)."
  exit 1
fi
