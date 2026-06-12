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
npm test -- --run src/screens/__tests__/SimulationCheck.external.test.tsx
npm run build
```

Expected: Screen 02 external integration tests pass and production build succeeds.

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

- Uses `docker-compose.yml:docker-compose.a4000.yml`.
- Local OrbStack may override CPU caps with `SIL_ORCHESTRATOR_CPUS`, `SIL_NODES_CPUS`, and `FOXGLOVE_CPUS`; A4000 defaults remain in `docker-compose.a4000.yml`.
- Generates local dev TLS certs at `certs/sil.crt` and `certs/sil.key` when absent.
- `docker compose config -q` passes.
- `sil-orchestrator`, `sil-nodes`, and `foxglove-bridge` build and start locally.
- `https://127.0.0.1:18000/api/v1/health` returns `{"status":"ok"}`.
- `/api/v1/integration/profiles` responds.
- `/api/v1/integration/probe` writes `runs/local_a4000_container_probe_*.json`.
- `sil-nodes` has `ROS_DOMAIN_ID=42`.

## A4000 Narrow Deploy

Only after local OrbStack gate passes, copy touched paths to the existing A4000 TDL checkout.

Example destination, adjust only if the existing checkout differs:

```bash
A4000_TDL=/home/mass/MASS-L3-Tactical-Layer
rsync -av \
  config/integration_profiles \
  src/sil_orchestrator/integration \
  src/sil_orchestrator/main.py \
  src/sim_workbench/external_adapters \
  docker/sil_entrypoint.sh \
  docker/sil_orchestrator.Dockerfile \
  docker/sil_nodes.Dockerfile \
  scripts/integration/start_external_adapters.sh \
  scripts/local-a4000-env.sh \
  scripts/local-a4000-acceptance.sh \
  web/src/api/silApi.ts \
  web/src/screens/SimulationCheck.tsx \
  web/src/screens/shared/ExternalIntegrationPanel.tsx \
  web/src/screens/__tests__/SimulationCheck.external.test.tsx \
  docs/Design/SIL/external-module-adapter-spec.md \
  docs/Design/SIL/external-module-adapter-runbook.md \
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
