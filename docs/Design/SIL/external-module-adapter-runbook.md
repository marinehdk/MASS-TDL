# External Module Adapter Runbook

Date: 2026-06-12

## Rule

Run local validation first. Sync to A4000 only after the local OrbStack A4000-equivalent gate passes.

Do not use `git pull`, `git reset`, or broad repository sync on A4000. Use patch/scp/rsync for touched files only.

## Local Default Regression

```bash
PYTHONDONTWRITEBYTECODE=1 pytest \
  tests/sil_orchestrator/test_integration_profiles.py \
  tests/sil_orchestrator/test_integration_routes.py \
  tests/sim_workbench/external_adapters/test_converters.py \
  tests/sim_workbench/external_adapters/test_ipc.py \
  tests/sim_workbench/external_adapters/test_route_out.py \
  tests/scripts/test_start_external_adapters.py \
  -q
```

Expected: all tests pass.

## Frontend Regression

```bash
cd web
npm test -- --run src/screens/__tests__/SimulationCheck.runtime.test.tsx src/screens/runtime/__tests__
npm run build
```

Expected: Screen 02 `仿真检查` runtime console tests pass and production build succeeds.

## Screen 02 Runtime Readiness

Screen 02 `仿真检查` owns local runtime readiness through the Runtime Console.
The Runtime Console is the readiness surface for TDL core services and external plugin roles.

Expected readiness behavior:

- `/api/v1/runtime/summary` returns `active_profile`.
- `/api/v1/runtime/probe` writes `runs/local_runtime_probe_*.json` and returns `"verdict":"GO"`.
- Runtime profiles carry the backend mode contract:
  - `internal-local`: local internal TDL-only readiness.
  - `integration-local`: local external-plugin integration readiness, default for local gate.
  - `integration-a4000`: A4000 external-plugin integration readiness.
- The Screen 02 `内测/集成` switch is currently a frontend workflow selector/display; backend Runtime Console actions and probes remain pinned to `TDL_RUNTIME_PROFILE` until a backend profile-switch endpoint is added.
- TDL core services are visible as `core_service`.
- Plugin roles are visible as plugin service/role rows.
- Core services expose restart but no per-service stop.
- Plugin roles enforce one active plugin per role.

## ROS Package Build

Inside a ROS2 Humble environment:

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select external_adapters
```

Expected: `Summary: 1 package finished`.

## Local OrbStack A4000-Equivalent Gate

Run before any A4000 sync:

```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```

Expected:

- Uses `docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml`.
- Local OrbStack may override CPU caps with `SIL_ORCHESTRATOR_CPUS`, `SIL_NODES_CPUS`, and `FOXGLOVE_CPUS`; A4000 defaults remain in `docker-compose.a4000.yml`.
- Generates local dev TLS certs at `certs/sil.crt` and `certs/sil.key` when absent.
- `docker compose config -q` passes.
- If the local `mass-l3-sil` compose project points to another checkout, the gate fails fast by default. Stop that stack first, or rerun with `RECLAIM_STALE_LOCAL_PROJECT=1` to recreate it for the current checkout before collecting evidence.
- Core runtime services and the active `integration-local` plugin placeholders build and start locally.
- Inactive plugin candidates may be created but stopped so Screen 02 can hot-switch them later.
- `https://127.0.0.1:18000/api/v1/health` returns `{"status":"ok"}`.
- `/api/v1/integration/profiles` responds.
- `/api/v1/runtime/summary` returns `active_profile`.
- `/api/v1/runtime/probe` writes `runs/local_runtime_probe_*.json` and returns `"verdict":"GO"`.
- `/api/v1/integration/probe` writes `runs/local_a4000_container_probe_*.json`.
- TDL core services are visible as `core_service`.
- Plugin roles are visible as plugin service/role rows.
- Core services expose restart but no per-service stop.
- Plugin roles enforce one active plugin per role.
- Runtime Console container control requires the orchestrator container to mount `/var/run/docker.sock` through the A4000/local override, not base compose. Keep this surface on local/A4000 validation hosts and behind the existing orchestrator API boundary.
- `sil-nodes` has `ROS_DOMAIN_ID=42`.

## A4000 Narrow Deploy

Only after local OrbStack gate passes, copy touched paths to the existing A4000 TDL checkout.

Example destination, adjust only if the existing checkout differs:

```bash
A4000_TDL=/home/mass/MASS-L3-Tactical-Layer
rsync -avR \
  docker-compose.yml \
  docker-compose.a4000.yml \
  docker-compose.plugins.yml \
  docker/sil_orchestrator.Dockerfile \
  config/runtime_plugins \
  config/runtime_profiles \
  src/sil_orchestrator/runtime \
  scripts/local-a4000-env.sh \
  scripts/local-a4000-acceptance.sh \
  web/src/api/silApi.ts \
  web/src/screens/SimulationCheck.tsx \
  web/src/screens/runtime \
  web/src/screens/__tests__/SimulationCheck.runtime.test.tsx \
  docs/Design/SIL/external-module-adapter-runbook.md \
  docs/Design/SIL/external-module-adapter-development-ledger.md \
  mass@A4000:${A4000_TDL}/
```

If the checkout path is uncertain:

```bash
ssh mass@A4000 'pwd; ls -la /home/mass'
```

Do not create a second checkout.

## A4000 Probe

After narrow deploy and service restart on A4000:

```bash
source scripts/a4000-env.sh
curl -sk -X POST "${ORCH_URL}/api/v1/integration/profile" \
  -H 'Content-Type: application/json' \
  -d '{"name":"a4000_external"}'

curl -sk -X POST "${ORCH_URL}/api/v1/integration/probe" \
  | tee runs/a4000_external_adapter_probe_$(date +%Y%m%d_%H%M%S).json
```

Expected:

- `profile_name` is `a4000_external`.
- Profile gate passes.
- Low-level control forbidden gate passes.
- Required topic gates pass when external modules run in configured domains.

If `all_clear` is false, keep the JSON evidence and fix only the failing gate.
