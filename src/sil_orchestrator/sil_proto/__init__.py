"""Generated Protobuf stubs for SIL messages and services."""

# Message types
from sil_proto.own_ship_state_pb2 import OwnShipState
from sil_proto.target_vessel_state_pb2 import TargetVesselState
from sil_proto.radar_measurement_pb2 import RadarMeasurement
from sil_proto.ais_message_pb2 import AISMessage
from sil_proto.environment_state_pb2 import EnvironmentState
from sil_proto.fault_event_pb2 import FaultEvent
from sil_proto.module_pulse_pb2 import ModulePulse
from sil_proto.scoring_row_pb2 import ScoringRow
from sil_proto.asdr_event_pb2 import ASDREvent
from sil_proto.lifecycle_status_pb2 import LifecycleStatus

# Service stubs (base classes from *_pb2_grpc)
from sil_proto.scenario_crud_pb2_grpc import ScenarioCRUDService
from sil_proto.lifecycle_control_pb2_grpc import LifecycleControlService
from sil_proto.sim_clock_pb2_grpc import SimClockService
from sil_proto.fault_trigger_pb2_grpc import FaultTriggerService
from sil_proto.self_check_pb2_grpc import SelfCheckService
from sil_proto.export_evidence_pb2_grpc import ExportEvidenceService

__all__ = [
    "OwnShipState",
    "TargetVesselState",
    "RadarMeasurement",
    "AISMessage",
    "EnvironmentState",
    "FaultEvent",
    "ModulePulse",
    "ScoringRow",
    "ASDREvent",
    "LifecycleStatus",
    "ScenarioCRUDService",
    "LifecycleControlService",
    "SimClockService",
    "FaultTriggerService",
    "SelfCheckService",
    "ExportEvidenceService",
]
