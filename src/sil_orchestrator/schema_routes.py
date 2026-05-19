from fastapi import APIRouter, HTTPException
from pathlib import Path
import json

router = APIRouter(prefix="/api/v1/schema", tags=["schema"])

# Path to the schema file in the scenarios root
SCENARIO_DIR = Path("/var/sil/scenarios") if Path("/var/sil/scenarios").exists() else Path(__file__).resolve().parent.parent.parent / "scenarios"
SCHEMA_FILE = SCENARIO_DIR / "fcb_traffic_situation.schema.json"

@router.get("/fcb_traffic_situation")
async def get_fcb_schema():
    if not SCHEMA_FILE.exists():
        raise HTTPException(status_code=404, detail="Schema file not found")
    
    with open(SCHEMA_FILE, "r") as f:
        return json.load(f)
