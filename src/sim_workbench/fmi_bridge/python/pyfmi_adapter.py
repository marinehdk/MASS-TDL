# python/pyfmi_adapter.py
"""PyFMI wrapper for FMI 2.0 Co-Simulation FMUs using fmpy.

Phase 1: Python runtime FMU (pythonfmu-generated) — accepted for DEMO-1 tool qualification.
Phase 2: C++ FMI Library wrapper for certification evidence.
"""
from __future__ import annotations
from pathlib import Path
from typing import Optional
import zipfile
import tempfile
import os
import shutil


class PyFmiAdapter:
    """Load and step an FMI 2.0 Co-Simulation FMU via fmpy."""

    def __init__(self, fmu_path: str, instance_name: str = "instance",
                 step_size: float = 0.02) -> None:
        self._fmu_path = Path(fmu_path)
        self._instance_name = instance_name
        self._step_size = step_size
        self._fmu_instance: Optional[object] = None
        self._model_desc: Optional[dict] = None
        self._var_map: dict[str, int] = {}  # name -> value_ref
        self._unzipped_dir: Optional[str] = None
        self._loaded = False

    @property
    def instance_name(self) -> str:
        return self._instance_name

    @property
    def loaded(self) -> bool:
        return self._loaded

    def load(self) -> None:
        """Load FMU: unzip, read modelDescription.xml, create fmpy instance."""
        if not self._fmu_path.exists():
            raise FileNotFoundError(f"FMU file not found: {self._fmu_path}")

        self._unzipped_dir = tempfile.mkdtemp(prefix="fmu_")
        with zipfile.ZipFile(self._fmu_path, 'r') as zf:
            zf.extractall(self._unzipped_dir)

        model_desc_xml = os.path.join(self._unzipped_dir, "modelDescription.xml")
        if not os.path.exists(model_desc_xml):
            raise RuntimeError(f"modelDescription.xml not found in {self._fmu_path}")

        import xml.etree.ElementTree as ET
        tree = ET.parse(model_desc_xml)
        root = tree.getroot()
        fmi_version = root.attrib.get("fmiVersion", "")
        if not fmi_version.startswith("2.0"):
            raise RuntimeError(f"Unsupported FMI version: {fmi_version}")

        self._model_desc = {
            "model_name": root.attrib.get("modelName", "unknown"),
            "guid": root.attrib.get("guid", "unknown"),
            "fmi_version": fmi_version,
        }

        for mv in root.findall(".//ScalarVariable"):
            name = mv.attrib.get("name", "")
            vr = int(mv.attrib.get("valueReference", "-1"))
            self._var_map[name] = vr

        from fmpy import instantiate_fmu, read_model_description
        model = read_model_description(model_desc_xml)
        self._fmu_instance = instantiate_fmu(
            unzipdir=self._unzipped_dir,
            model_description=model,
            fmi_type='CoSimulation',
        )
        self._loaded = True

    def setup_experiment(self, start_time: float = 0.0,
                         stop_time: Optional[float] = None,
                         tolerance: float = 1e-6) -> None:
        if not self._loaded:
            raise RuntimeError("FMU not loaded. Call load() first.")
        self._fmu_instance.setupExperiment(
            startTime=start_time,
            stopTime=stop_time,
            tolerance=tolerance,
        )

    def enter_initialization_mode(self) -> None:
        self._fmu_instance.enterInitializationMode()

    def exit_initialization_mode(self) -> None:
        self._fmu_instance.exitInitializationMode()

    def do_step(self, current_time: float, step_size: float) -> bool:
        self._fmu_instance.doStep(
            currentCommunicationPoint=current_time,
            communicationStepSize=step_size,
        )
        return True

    def get_real(self, var_name: str) -> float:
        if var_name not in self._var_map:
            raise KeyError(f"Variable '{var_name}' not found in FMU")
        vr = self._var_map[var_name]
        return self._fmu_instance.getReal([vr])[0]

    def set_real(self, var_name: str, value: float) -> None:
        if var_name not in self._var_map:
            raise KeyError(f"Variable '{var_name}' not found in FMU")
        vr = self._var_map[var_name]
        self._fmu_instance.setReal([vr], [value])

    def get_variable_names(self) -> list[str]:
        return list(self._var_map.keys())

    def terminate(self) -> None:
        if self._fmu_instance is not None:
            self._fmu_instance.terminate()
            self._fmu_instance = None
        if self._unzipped_dir and os.path.exists(self._unzipped_dir):
            shutil.rmtree(self._unzipped_dir, ignore_errors=True)
        self._loaded = False

    def __del__(self):
        self.terminate()
