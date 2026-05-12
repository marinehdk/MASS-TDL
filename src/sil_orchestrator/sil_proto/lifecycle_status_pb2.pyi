import datetime

from google.protobuf import timestamp_pb2 as _timestamp_pb2
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class LifecycleStatus(_message.Message):
    __slots__ = ("stamp", "current_state", "scenario_id", "scenario_hash", "sim_time", "wall_time", "sim_rate")
    class LifecycleState(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        LIFECYCLE_STATE_UNSPECIFIED: _ClassVar[LifecycleStatus.LifecycleState]
        LIFECYCLE_STATE_UNCONFIGURED: _ClassVar[LifecycleStatus.LifecycleState]
        LIFECYCLE_STATE_INACTIVE: _ClassVar[LifecycleStatus.LifecycleState]
        LIFECYCLE_STATE_ACTIVE: _ClassVar[LifecycleStatus.LifecycleState]
        LIFECYCLE_STATE_DEACTIVATING: _ClassVar[LifecycleStatus.LifecycleState]
        LIFECYCLE_STATE_FINALIZED: _ClassVar[LifecycleStatus.LifecycleState]
    LIFECYCLE_STATE_UNSPECIFIED: LifecycleStatus.LifecycleState
    LIFECYCLE_STATE_UNCONFIGURED: LifecycleStatus.LifecycleState
    LIFECYCLE_STATE_INACTIVE: LifecycleStatus.LifecycleState
    LIFECYCLE_STATE_ACTIVE: LifecycleStatus.LifecycleState
    LIFECYCLE_STATE_DEACTIVATING: LifecycleStatus.LifecycleState
    LIFECYCLE_STATE_FINALIZED: LifecycleStatus.LifecycleState
    STAMP_FIELD_NUMBER: _ClassVar[int]
    CURRENT_STATE_FIELD_NUMBER: _ClassVar[int]
    SCENARIO_ID_FIELD_NUMBER: _ClassVar[int]
    SCENARIO_HASH_FIELD_NUMBER: _ClassVar[int]
    SIM_TIME_FIELD_NUMBER: _ClassVar[int]
    WALL_TIME_FIELD_NUMBER: _ClassVar[int]
    SIM_RATE_FIELD_NUMBER: _ClassVar[int]
    stamp: _timestamp_pb2.Timestamp
    current_state: LifecycleStatus.LifecycleState
    scenario_id: str
    scenario_hash: str
    sim_time: float
    wall_time: float
    sim_rate: float
    def __init__(self, stamp: _Optional[_Union[datetime.datetime, _timestamp_pb2.Timestamp, _Mapping]] = ..., current_state: _Optional[_Union[LifecycleStatus.LifecycleState, str]] = ..., scenario_id: _Optional[str] = ..., scenario_hash: _Optional[str] = ..., sim_time: _Optional[float] = ..., wall_time: _Optional[float] = ..., sim_rate: _Optional[float] = ...) -> None: ...
