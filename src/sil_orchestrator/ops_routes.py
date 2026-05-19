"""Ops control endpoints — Quick Fix action backend"""
import re, time, asyncio, json
from pathlib import Path
from fastapi import APIRouter, HTTPException, Query
from sil_orchestrator.config import SCENARIO_DIR, RUN_DIR

router = APIRouter(prefix="/api/v1/ops")
_NAME_PATTERN = re.compile(r"^[a-zA-Z0-9_-]{1,64}$")
_AUDIT_LOG = RUN_DIR / "ops_audit.jsonl"

def _validate_name(name: str) -> str:
    if not _NAME_PATTERN.match(name):
        raise HTTPException(status_code=422, detail=f"Invalid name pattern: {name}")
    return name

def _audit(action: str, params: dict, success: bool) -> None:
    try:
        _AUDIT_LOG.parent.mkdir(parents=True, exist_ok=True)
        with open(_AUDIT_LOG, "a") as f:
            f.write(json.dumps({
                "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "action": action, "params": params, "success": success,
            }) + "\n")
    except OSError:
        pass  # audit log is best-effort; never fail the request

async def _run(cmd: list[str], timeout: float) -> tuple[bool, str]:
    try:
        proc = await asyncio.create_subprocess_exec(*cmd,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=timeout)
        ok = proc.returncode == 0
        return ok, (stdout.decode()[:500] if ok else stderr.decode()[:500])
    except asyncio.TimeoutError:
        return False, f"timeout after {timeout}s"

@router.post("/restart_node")
async def restart_node(name: str = Query(...)):
    _validate_name(name)
    t0 = time.monotonic()
    ok, msg = await _run(["docker", "restart", f"$(docker ps -q --filter name={name})"], timeout=15.0)
    _audit("restart_node", {"name": name}, ok)
    return {"success": ok, "message": msg, "duration_ms": round((time.monotonic()-t0)*1000, 1)}

@router.post("/restart_services")
async def restart_services():
    t0 = time.monotonic()
    ok, msg = await _run(["docker", "compose", "restart"], timeout=30.0)
    _audit("restart_services", {}, ok)
    return {"success": ok, "message": msg, "duration_ms": round((time.monotonic()-t0)*1000, 1)}

@router.post("/sync_time")
async def sync_time():
    t0 = time.monotonic()
    ok, msg = await _run(["chronyc", "makestep"], timeout=5.0)
    _audit("sync_time", {}, ok)
    return {"success": ok, "message": msg, "duration_ms": round((time.monotonic()-t0)*1000, 1)}

@router.post("/clear_hash_cache")
async def clear_hash_cache(scenario_id: str = Query(...)):
    t0 = time.monotonic()
    cache_path = SCENARIO_DIR / scenario_id / ".hash_cache"
    msg = "no hash_cache for " + scenario_id
    if cache_path.exists():
        cache_path.unlink()
        msg = "hash_cache cleared for " + scenario_id
    _audit("clear_hash_cache", {"scenario_id": scenario_id}, True)
    return {"success": True, "message": msg, "duration_ms": round((time.monotonic()-t0)*1000, 1)}

@router.post("/ensure_asdr_dir")
async def ensure_asdr_dir(run_id: str = Query(...)):
    t0 = time.monotonic()
    preflight_dir = RUN_DIR / run_id / "preflight"
    preflight_dir.mkdir(parents=True, exist_ok=True)
    preflight_dir.chmod(0o755)
    _audit("ensure_asdr_dir", {"run_id": run_id}, True)
    return {"success": True, "message": f"ensured: {preflight_dir}", "duration_ms": round((time.monotonic()-t0)*1000, 1)}


@router.get("/compose_content")
async def compose_content():
    """Read the docker-compose.yml file content and return it to the frontend code viewer."""
    try:
        # Check standard relative paths or absolute path
        paths = [
            Path("docker-compose.yml"),
            Path(__file__).resolve().parents[2] / "docker-compose.yml",
            Path("/Users/marine/Code/MASS-L3-Tactical Layer/docker-compose.yml")
        ]
        
        for p in paths:
            if p.exists():
                return {"success": True, "content": p.read_text()}
                
        return {"success": False, "message": "docker-compose.yml file not found"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
