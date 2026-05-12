from fastapi import APIRouter, HTTPException
from sil_orchestrator.scenario_store import ScenarioStore

router = APIRouter(prefix="/api/v1/scenarios")
store = ScenarioStore()


@router.get("")
async def list_scenarios():
    return store.list()


@router.get("/{scenario_id}")
async def get_scenario(scenario_id: str):
    result = store.get(scenario_id)
    if result is None:
        raise HTTPException(status_code=404, detail="Scenario not found")
    return result


@router.post("")
async def create_scenario(request: dict):
    yaml_content = request.get("yaml_content", "")
    return store.create(yaml_content)


@router.put("/{scenario_id}")
async def update_scenario(scenario_id: str, request: dict):
    result = store.update(scenario_id, request.get("yaml_content", ""))
    if result is None:
        raise HTTPException(status_code=404, detail="Scenario not found")
    return result


@router.delete("/{scenario_id}")
async def delete_scenario(scenario_id: str):
    if not store.delete(scenario_id):
        raise HTTPException(status_code=404, detail="Scenario not found")
    return {"deleted": True}


@router.post("/validate")
async def validate_scenario(request: dict):
    yaml_content = request.get("yaml_content", "")
    return store.validate(yaml_content)
