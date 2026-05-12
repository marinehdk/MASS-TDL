"""SIL Orchestrator configuration."""
import os
from pathlib import Path

SCENARIO_DIR = Path(os.getenv("SIL_SCENARIO_DIR", "/var/sil/scenarios"))
RUN_DIR = Path(os.getenv("SIL_RUN_DIR", "/var/sil/runs"))
EXPORT_DIR = Path(os.getenv("SIL_EXPORT_DIR", "/var/sil/exports"))
ROS_DOMAIN_ID = int(os.getenv("ROS_DOMAIN_ID", "0"))
