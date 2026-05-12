import datetime

from google.protobuf import timestamp_pb2 as _timestamp_pb2
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class ModulePulse(_message.Message):
    __slots__ = ("stamp", "module_id", "state", "latency_ms", "message_drops")
    class ModuleId(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        MODULE_ID_UNSPECIFIED: _ClassVar[ModulePulse.ModuleId]
        MODULE_ID_M1: _ClassVar[ModulePulse.ModuleId]
        MODULE_ID_M2: _ClassVar[ModulePulse.ModuleId]
        MODULE_ID_M3: _ClassVar[ModulePulse.ModuleId]
        MODULE_ID_M4: _ClassVar[ModulePulse.ModuleId]
        MODULE_ID_M5: _ClassVar[ModulePulse.ModuleId]
        MODULE_ID_M6: _ClassVar[ModulePulse.ModuleId]
        MODULE_ID_M7: _ClassVar[ModulePulse.ModuleId]
        MODULE_ID_M8: _ClassVar[ModulePulse.ModuleId]
    MODULE_ID_UNSPECIFIED: ModulePulse.ModuleId
    MODULE_ID_M1: ModulePulse.ModuleId
    MODULE_ID_M2: ModulePulse.ModuleId
    MODULE_ID_M3: ModulePulse.ModuleId
    MODULE_ID_M4: ModulePulse.ModuleId
    MODULE_ID_M5: ModulePulse.ModuleId
    MODULE_ID_M6: ModulePulse.ModuleId
    MODULE_ID_M7: ModulePulse.ModuleId
    MODULE_ID_M8: ModulePulse.ModuleId
    class HealthState(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        HEALTH_STATE_UNSPECIFIED: _ClassVar[ModulePulse.HealthState]
        HEALTH_STATE_GREEN: _ClassVar[ModulePulse.HealthState]
        HEALTH_STATE_AMBER: _ClassVar[ModulePulse.HealthState]
        HEALTH_STATE_RED: _ClassVar[ModulePulse.HealthState]
    HEALTH_STATE_UNSPECIFIED: ModulePulse.HealthState
    HEALTH_STATE_GREEN: ModulePulse.HealthState
    HEALTH_STATE_AMBER: ModulePulse.HealthState
    HEALTH_STATE_RED: ModulePulse.HealthState
    STAMP_FIELD_NUMBER: _ClassVar[int]
    MODULE_ID_FIELD_NUMBER: _ClassVar[int]
    STATE_FIELD_NUMBER: _ClassVar[int]
    LATENCY_MS_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_DROPS_FIELD_NUMBER: _ClassVar[int]
    stamp: _timestamp_pb2.Timestamp
    module_id: ModulePulse.ModuleId
    state: ModulePulse.HealthState
    latency_ms: int
    message_drops: int
    def __init__(self, stamp: _Optional[_Union[datetime.datetime, _timestamp_pb2.Timestamp, _Mapping]] = ..., module_id: _Optional[_Union[ModulePulse.ModuleId, str]] = ..., state: _Optional[_Union[ModulePulse.HealthState, str]] = ..., latency_ms: _Optional[int] = ..., message_drops: _Optional[int] = ...) -> None: ...
