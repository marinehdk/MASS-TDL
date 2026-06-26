# A4000 SIL Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move all CPU-intensive SIL work (backend docker stack + Vite HMI + Playwright harness) from the MacBook onto the company A4000 Ubuntu server so the 倍速/avoidance test harness runs deterministic and reproducible, free of host-desktop CPU contention.

**Architecture:** The backend (ROS2 Humble) runs in the existing docker compose stack with `network_mode: host` (required for CycloneDDS discovery). Because host networking binds container ports directly to host ports, and the A4000 already runs jitsi (host:8000) + another stack (host:8765), we remap our app bind ports via a compose **override file** (no Dockerfile rebuild) and isolate DDS with a non-default `ROS_DOMAIN_ID`. The frontend talks only through relative vite-proxy paths, so only `vite.config.ts` proxy *targets* change (made env-driven to avoid breaking local Mac dev). Playwright runs headless on the same clean server, so HMI-path RTF approaches the headless ~10× and becomes reproducible.

**Tech Stack:** Ubuntu 22.04.5, ROS2 Humble, Docker 29.1.3 + Compose v5, CycloneDDS, FastAPI/uvicorn, foxglove_bridge, Vite, Playwright, pm2, Node 20 (nvm).

**Server facts (verified 2026-06-02):** `ssh a4000` → 192.168.121.50, user `marine.huang` (sudo+docker groups). i7-12700 (20 threads), 125 GiB RAM, 521 GB free, 2× RTX A4000. Docker 29.1.3, Compose v5, nvidia-ctk 1.19, ROS2 Humble native, git+gitlab reachable (`git ls-remote` OK). Node v12.22.9 (TOO OLD), no pm2, repo not cloned. Ports busy: **8000 (jitsi-meet-web), 8765 (other)**; free: 80/443/5173/3000.

**Port plan:** orchestrator host **8000 → 18000**; foxglove host **8765 → 18765**; martin **3000** (free, keep); vite **5173** (free, keep, bind 0.0.0.0); `ROS_DOMAIN_ID` **0 → 42**.

---

## ⚠️ Shared-host guardrails (read before every task)

The A4000 runs **live production services** (jitsi-meet-*, fat-system-* — "Up 8 days") and may be a **shared account** (`marine.huang`). These are NOT ours.
- **NEVER** `docker compose down` without `-p mass-l3-sil` / project scoping. Always scope to our project name `mass-l3-sil`.
- **NEVER** stop/restart/remove any `jitsi-*`, `fat-system-*`, `init-dirs`, `avds-*` container.
- **NEVER** bind host ports 8000, 8765, 9443 (in use). Use our remapped 18000/18765.
- Our docker compose `name:` is `mass-l3-sil` (line 1 of docker-compose.yml) → containers are `mass-l3-sil-*`. Only touch those.

### Other-user / other-service impact mitigations (baked into tasks below)
1. **Shared account** → do NOT change the account's default Node. nvm installs Node 20 under `~/.nvm` (system node v12 untouched); we only `nvm use 20` explicitly in our commands, never `nvm alias default`.
2. **DDS must not leak to the LAN** → all 3 services get `ROS_LOCALHOST_ONLY=1` + `ROS_DOMAIN_ID=42`. Everything (sim/foxglove/vite/playwright) is on this one host, so DDS stays on loopback and cannot touch other machines or other people's ROS2.
3. **CPU starvation of resident services** → our containers get `cpus:` caps (sil-nodes 12, orchestrator 1.5, foxglove 1.5 ≈ 15/20 threads), leaving ~5 threads + OS for jitsi/fat-system.
4. **The only system-wide mutation is `playwright install-deps` (sudo apt)** → review the package list with a dry run before applying; apt only adds shared libs (no downgrade/removal).
5. **`pm2`** installs under nvm's node 20 (per-user), not system-wide.

---

## Task 0: Commit harness + sweep script to branch (run on LOCAL Mac)

**Files:**
- Modify: `scripts/rtf_headless_sweep.py` (make orchestrator URL env-driven)
- Already on branch (uncommitted): `web/e2e/mvp_consistency.spec.ts` (copied from feat/hmi-e2e-consistency-test)
- New: `scripts/rtf_headless_sweep.py`

- [ ] **Step 1: Make sweep BASE env-driven**

In `scripts/rtf_headless_sweep.py`, replace the hardcoded BASE line:

```python
BASE = os.environ.get("ORCH_URL", "https://127.0.0.1:8000") + "/api/v1"
```

(ensure `import os` is present — it is, added earlier for SETTLE_S/SAMPLE_S).

- [ ] **Step 2: Verify branch + files**

Run: `git -C "/Users/marine/Code/MASS-L3-Tactical Layer" status --short web/e2e/mvp_consistency.spec.ts scripts/rtf_headless_sweep.py`
Expected: both listed (one `??`/`A`, one modified/`??`).

- [ ] **Step 3: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add web/e2e/mvp_consistency.spec.ts scripts/rtf_headless_sweep.py
git commit -m "test(e2e): bring MVP consistency harness + headless RTF sweep onto trace-fix branch

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

- [ ] **Step 4: Push**

Run: `git push -u origin fix/debug-snapshot-stale-trace`
Expected: branch pushed; note the commit SHA for server checkout.

---

## Task 1: Server bootstrap — Node 20, pm2, Playwright OS deps

**Files:** none (server-side install). All steps run via `ssh a4000 '<cmd>'`.

- [ ] **Step 1: Install nvm + Node 20 (do NOT change account default — shared account)**

```bash
ssh a4000 'curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh | bash'
ssh a4000 'export NVM_DIR=$HOME/.nvm && . $NVM_DIR/nvm.sh && nvm install 20 && nvm use 20 && node --version'
ssh a4000 'node --version'   # second call WITHOUT sourcing nvm — must still show system v12
```
Expected: first → `v20.x.x`; second (fresh shell, no nvm) → `v12.22.9` (proves we did not change the account default).

- [ ] **Step 2: Install pm2 globally**

```bash
ssh a4000 'export NVM_DIR=$HOME/.nvm && . $NVM_DIR/nvm.sh && nvm use 20 >/dev/null && npm i -g pm2 && pm2 --version'
```
Expected: a pm2 version string.

- [ ] **Step 3: Review Playwright OS deps BEFORE installing (only system-wide change)**

```bash
ssh a4000 'export NVM_DIR=$HOME/.nvm && . $NVM_DIR/nvm.sh && nvm use 20 >/dev/null && npx --yes playwright@latest install-deps --dry-run chromium 2>&1 | head -30'
```
Expected: prints the apt-get install command + package list (libnss3, libatk1.0-0, libgbm1, libasound2, …). Confirm it only ADDS libs (no remove/downgrade). Most are likely already present from jitsi/desktop.

- [ ] **Step 4: Install Playwright chromium OS dependencies (needs sudo)**

```bash
ssh a4000 'export NVM_DIR=$HOME/.nvm && . $NVM_DIR/nvm.sh && nvm use 20 >/dev/null && sudo env "PATH=$PATH" npx --yes playwright@latest install-deps chromium'
```
Expected: apt installs the reviewed libs, ends "Done".
(If sudo prompts for password, it is `marine.huang`.)

---

## Task 2: Clone repo + checkout branch + generate certs

**Files:**
- Create on server: `~/Code/mass-l3/` (clone)
- Create on server: `~/Code/mass-l3/certs/sil.crt`, `certs/sil.key`

- [ ] **Step 1: Clone**

```bash
ssh a4000 'mkdir -p ~/Code && cd ~/Code && git clone git@gitlab.sangoai.com:mass_devgroup/01-dynamics/01-simulation.git mass-l3 && cd mass-l3 && git fetch origin && git checkout fix/debug-snapshot-stale-trace && git log --oneline -3'
```
Expected: shows the Task 0 commit at HEAD.

- [ ] **Step 2: Generate self-signed certs (gitignored, not in clone)**

```bash
ssh a4000 'cd ~/Code/mass-l3 && openssl req -x509 -newkey rsa:2048 -nodes -keyout certs/sil.key -out certs/sil.crt -days 825 -subj "/CN=sil-local" -addext "subjectAltName=IP:127.0.0.1,IP:192.168.121.50,DNS:localhost" && ls -la certs/'
```
Expected: `sil.crt` + `sil.key` present.

- [ ] **Step 3: Install web deps**

```bash
ssh a4000 'export NVM_DIR=$HOME/.nvm && . $NVM_DIR/nvm.sh && nvm use 20 >/dev/null && cd ~/Code/mass-l3/web && npm ci && npx playwright install chromium'
```
Expected: deps installed, chromium downloaded.

---

## Task 3: Port remap + DDS isolation (compose override + env-driven vite)

**Files:**
- Create: `docker-compose.a4000.yml`
- Modify: `web/vite.config.ts:6-8,22` (env-driven ports + host)
- Create: `scripts/a4000-env.sh`

- [ ] **Step 1: Create compose override**

Create `docker-compose.a4000.yml`:

```yaml
# A4000 shared-host override: remap app bind ports off jitsi(8000)/other(8765)
# and isolate DDS traffic. Apply with: docker compose -f docker-compose.yml -f docker-compose.a4000.yml ...
# (or set COMPOSE_FILE — see scripts/a4000-env.sh)
services:
  sil-orchestrator:
    cpus: 1.5                       # cap: good neighbour on shared host
    command:
      - /bin/bash
      - -c
      - "source /opt/ros/humble/setup.bash && source /opt/sil/install/setup.bash && python3 -m uvicorn sil_orchestrator.main:app --host 0.0.0.0 --port 18000 --ssl-certfile /certs/sil.crt --ssl-keyfile /certs/sil.key"
    environment:
      - ROS_DOMAIN_ID=42
      - ROS_LOCALHOST_ONLY=1        # DDS stays on loopback — never touches LAN/other users

  sil-nodes:
    cpus: 12.0                      # heaviest (250Hz clock + RK4 + all M-nodes); leaves ~5 threads for jitsi/fat
    environment:
      - ROS_DOMAIN_ID=42
      - ROS_LOCALHOST_ONLY=1

  foxglove-bridge:
    cpus: 1.5
    command:
      - bash
      - -c
      - |
        source /opt/ros/humble/setup.bash &&
        source /opt/ws/install/setup.bash &&
        ros2 run foxglove_bridge foxglove_bridge \
          --ros-args \
          -p port:=18765 \
          -p num_threads:=4 \
          -p send_buffer_limit:=10000000 \
          -p use_sim_time:=true \
          -p certfile:=/certs/sil.crt \
          -p keyfile:=/certs/sil.key \
          -p topic_whitelist:="[/sim_clock,/sil/own_ship_state,/sil/target_vessel_state,/sil/radar_meas,/sil/ais_msg,/sil/environment,/sil/tracked_targets,/sil/lifecycle_status,/sil/module_pulse,/sil/scoring,/sil/asdr_event,/sil/sat2_data,/sil/sat3_data,/sil/sotif_metrics,/l3/fsm_state]"
    environment:
      - ROS_DOMAIN_ID=42
      - ROS_LOCALHOST_ONLY=1
```

> Note: `ROS_LOCALHOST_ONLY=1` works because all services use `network_mode: host` and share the host loopback — they still discover each other on `lo`, but DDS multicast never leaves the box. CPU caps are cgroup-based and independent of host networking. The 12-thread cap on sil-nodes may bound the RTF ceiling near ~12-14× (still ≥ our 10× target).

- [ ] **Step 2: Make vite ports env-driven (non-breaking for local)**

In `web/vite.config.ts`, replace lines 6-8:

```typescript
const isDocker = fs.existsSync('/.dockerenv');
const ORCH_PORT = process.env.ORCH_PORT ?? '8000';
const FOX_PORT = process.env.FOX_PORT ?? '8765';
const host = isDocker ? 'host.docker.internal' : '127.0.0.1';
const target = `https://${host}:${ORCH_PORT}`;
const wsTarget = `wss://${host}:${ORCH_PORT}`;
```

In `web/vite.config.ts`, replace the `/foxglove-ws` target (line 22):

```typescript
        target: `ws://${host}:${FOX_PORT}`,
```

In `web/vite.config.ts`, add host binding inside `server:` (after `port: 5173,`):

```typescript
    host: process.env.VITE_HOST ?? 'localhost',
```

- [ ] **Step 3: Create server env file**

Create `scripts/a4000-env.sh`:

```bash
#!/usr/bin/env bash
# Source before `npm run sys:start` on the A4000 server.
# Remaps our ports off the shared host's jitsi(8000)/other(8765) and isolates DDS.
export COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml
export ROS_DOMAIN_ID=42
export ROS_LOCALHOST_ONLY=1
export ORCH_PORT=18000
export FOX_PORT=18765
export ORCH_URL=https://127.0.0.1:18000
export VITE_HOST=0.0.0.0
```

- [ ] **Step 4: Commit + push (local Mac), then pull on server**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add docker-compose.a4000.yml web/vite.config.ts scripts/a4000-env.sh
git commit -m "feat(sil): A4000 shared-host compose override + env-driven vite ports

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
git push
ssh a4000 'cd ~/Code/mass-l3 && git pull --ff-only'
```
Expected: server HEAD includes this commit.

- [ ] **Step 5: Verify no local regression**

Run (local Mac): `cd "/Users/marine/Code/MASS-L3-Tactical Layer/web" && node -e "process.env.ORCH_PORT=undefined; require('child_process')" 2>/dev/null; grep -n "8000\|ORCH_PORT" vite.config.ts`
Expected: defaults still `8000`/`8765` when env unset (local Mac unaffected).

---

## Task 4: Build images on server

**Files:** none (build).

- [ ] **Step 1: Build the three images (scoped to our project)**

```bash
ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && docker compose build 2>&1 | tail -20'
```
Expected: sil_orchestrator + sil_nodes build OK (colcon build via BuildKit cache; first build several minutes on 20 cores). Ends without error.

- [ ] **Step 2: Confirm only our images created**

```bash
ssh a4000 'docker images --format "{{.Repository}}:{{.Tag}}" | grep mass-l3-sil'
```
Expected: `mass-l3-sil-sil-orchestrator`, `mass-l3-sil-sil-nodes` listed. jitsi/fat-system untouched.

---

## Task 5: Start stack + headless RTF verification

**Files:** none.

- [ ] **Step 1: Start backend only (scoped), confirm no port clash**

```bash
ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && docker compose up -d && sleep 30 && docker compose ps'
```
Expected: `mass-l3-sil-sil-orchestrator-1`, `-sil-nodes-1`, `-foxglove-bridge-1`, `-martin-tile-server-1` all Up. No errors about address-in-use.

- [ ] **Step 2: Confirm jitsi/fat-system still alive (did not disrupt)**

```bash
ssh a4000 'docker ps --format "{{.Names}} {{.Status}}" | grep -E "jitsi|fat-system"'
```
Expected: all still "Up".

- [ ] **Step 3: Orchestrator reachable on 18000**

```bash
ssh a4000 'curl -sk https://127.0.0.1:18000/api/v1/health'
```
Expected: `{"status":"ok"}`

- [ ] **Step 4: Wait for ROS2 nodes (23) up**

```bash
ssh a4000 'docker exec mass-l3-sil-sil-nodes-1 bash -c "source /opt/ros/humble/setup.bash; source /opt/ws/install/setup.bash; export ROS_DOMAIN_ID=42; export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp; timeout 12 ros2 node list 2>/dev/null | wc -l"'
```
Expected: `23`

- [ ] **Step 5: Headless RTF sweep — expect higher ceiling than Mac (~14×)**

```bash
ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && python3 scripts/rtf_headless_sweep.py colreg-rule14-ho 5 10'
ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && SETTLE_S=1 SAMPLE_S=4 INTERVAL=0.25 python3 scripts/rtf_headless_sweep.py colreg-rule14-ho 20 30'
```
Expected: 5×→~5.0× (100%), 10×→~10.0× (100%); 20×/30× show the server's CPU ceiling (≥14×, higher than Mac since 20 threads + no desktop). Record the numbers.

---

## Task 6: Run harness on server, tighten band, confirm green (= task 2 DONE)

**Files:**
- Modify: `web/e2e/mvp_consistency.spec.ts` (RTF_BAND, only AFTER measuring)

- [ ] **Step 1: Start frontend (pm2) on server, bound 0.0.0.0**

```bash
ssh a4000 'export NVM_DIR=$HOME/.nvm && . $NVM_DIR/nvm.sh && nvm use 20 >/dev/null && cd ~/Code/mass-l3 && source scripts/a4000-env.sh && pm2 delete sil-frontend 2>/dev/null; pm2 start npm --name sil-frontend -- run dev --prefix web && sleep 6 && curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:5173'
```
Expected: `200`.

- [ ] **Step 2: First harness run to MEASURE HMI-path RTF (band still [2,13])**

```bash
ssh a4000 'export NVM_DIR=$HOME/.nvm && . $NVM_DIR/nvm.sh && nvm use 20 >/dev/null && cd ~/Code/mass-l3/web && RATE=10 ORCH_PORT=18000 FOX_PORT=18765 npx playwright test e2e/mvp_consistency.spec.ts --reporter=line 2>&1 | grep -E "A_rtf|A_turn|A_recon|passed|failed"'
```
Expected: `A_rtf` regressed RTF printed. On the clean server it should be far above the Mac's 1.59× — close to headless 10×. Record A_rtf, A_turn, A_recon.

- [ ] **Step 3: Tighten RTF_BAND to the measured-honest range**

In `web/e2e/mvp_consistency.spec.ts`, update `RTF_BAND` based on Step 2's measured value. If A_rtf ≈ 9–10× on the clean server, set:

```typescript
const RTF_BAND: [number, number] = [7.0, 12.0];
```

Update the band-rationale comment above it to: `// On the dedicated A4000 (no desktop contention) HMI-path RTF tracks headless ~10x; band catches both stuck-at-1x and runaway.`

(If Step 2 shows the server still can't sustain ≥7× under HMI load, DO NOT force the band — STOP and report; that is a real foxglove-WS bottleneck for separate systematic-debugging, not a band-tuning problem.)

- [ ] **Step 4: Re-run harness, confirm all 3 green**

```bash
ssh a4000 'export NVM_DIR=$HOME/.nvm && . $NVM_DIR/nvm.sh && nvm use 20 >/dev/null && cd ~/Code/mass-l3/web && RATE=10 ORCH_PORT=18000 FOX_PORT=18765 npx playwright test e2e/mvp_consistency.spec.ts --reporter=line 2>&1 | tail -15'
```
Expected: `1 passed`. **This is the reproducible green = task 2 complete.**

- [ ] **Step 5: Reproducibility check — run 3× back-to-back**

```bash
ssh a4000 'export NVM_DIR=$HOME/.nvm && . $NVM_DIR/nvm.sh && nvm use 20 >/dev/null && cd ~/Code/mass-l3/web && for i in 1 2 3; do RATE=10 ORCH_PORT=18000 FOX_PORT=18765 npx playwright test e2e/mvp_consistency.spec.ts --reporter=line 2>&1 | grep -E "A_rtf:|passed|failed"; done'
```
Expected: 3× passed, A_rtf values cluster tightly (the determinism win).

- [ ] **Step 6: Commit band change + push**

```bash
ssh a4000 'cd ~/Code/mass-l3 && git add web/e2e/mvp_consistency.spec.ts && git commit -m "test(e2e): tighten RTF band to A4000 clean-host envelope" && git push'
```

---

## Task 7: Remote HMI access verification

**Files:** none.

- [ ] **Step 1: Reach HMI from the Mac browser over LAN**

Run (local Mac): `curl -s -o /dev/null -w "%{http_code}\n" http://192.168.121.50:5173`
Expected: `200`. Then open `http://192.168.121.50:5173` in Chrome on the Mac — HMI loads, telemetry populates (backend now on the server). If `000`/refused, fall back to SSH tunnel: `ssh -N -L 5173:127.0.0.1:5173 a4000` then use `http://localhost:5173`.

- [ ] **Step 2: Document the access method**

Append to project notes / CLAUDE.md §13 the A4000 run recipe: `ssh a4000; cd ~/Code/mass-l3; source scripts/a4000-env.sh; npm run sys:start` then browse `http://192.168.121.50:5173`.

---

## Out of scope (next plan)

- **Task 3 of the user's request — freezing the test flow into an enhanced /systematic-debugging workflow** for hunting the avoidance failure. Depends on this migration landing green. MUST include fixing the **A_turn false-green**: heading range(max−min) greens on 0°↔360° open-loop oscillation (net avoidance ≈ 0); replace with net-heading-change / sustained-turn criterion. See memory `l3-avoidance-root-cause-m4-degenerate-window` and `mvp-e2e-consistency-harness`.
- Merging `feat/sim-speed-determinism` (23 commits: determinism overhaul + M5/M3 avoidance fixes) — deferred per "minimal union" decision.

---

## Self-Review

- **Spec coverage:** P0 commit/push (Task 0) ✓; Node/pm2/playwright bootstrap (Task 1) ✓; clone+certs (Task 2) ✓; port remap + DDS isolation + env-driven vite (Task 3) ✓; build (Task 4) ✓; start + headless RTF (Task 5) ✓; harness green + band tighten + reproducibility (Task 6) ✓; remote HMI (Task 7) ✓. task-3 workflow explicitly deferred ✓.
- **Placeholder scan:** all commands concrete with expected output; band value is conditional-on-measurement (Task 6 Step 3) with an explicit STOP guard rather than a blind number — intentional, not a placeholder.
- **Consistency:** ports 18000/18765 used consistently across compose override, a4000-env.sh, vite env, sweep ORCH_URL, and harness invocations; `ROS_DOMAIN_ID=42` consistent across all 3 services + the node-list verification; project scope `mass-l3-sil` consistent in guardrails.
