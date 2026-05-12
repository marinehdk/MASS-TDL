import datetime

from google.protobuf import timestamp_pb2 as _timestamp_pb2
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class EnvironmentState(_message.Message):
    __slots__ = ("stamp", "wind", "current", "visibility_nm", "sea_state_beaufort")
    class Wind(_message.Message):
        __slots__ = ("direction", "speed_mps")
        DIRECTION_FIELD_NUMBER: _ClassVar[int]
        SPEED_MPS_FIELD_NUMBER: _ClassVar[int]
        direction: float
        speed_mps: float
        def __init__(self, direction: _Optional[float] = ..., speed_mps: _Optional[float] = ...) -> None: ...
    class Current(_message.Message):
        __slots__ = ("direction", "speed_mps")
        DIRECTION_FIELD_NUMBER: _ClassVar[int]
        SPEED_MPS_FIELD_NUMBER: _ClassVar[int]
        direction: float
        speed_mps: float
        def __init__(self, direction: _Optional[float] = ..., speed_mps: _Optional[float] = ...) -> None: ...
    STAMP_FIELD_NUMBER: _ClassVar[int]
    WIND_FIELD_NUMBER: _ClassVar[int]
    CURRENT_FIELD_NUMBER: _ClassVar[int]
    VISIBILITY_NM_FIELD_NUMBER: _ClassVar[int]
    SEA_STATE_BEAUFORT_FIELD_NUMBER: _ClassVar[int]
    stamp: _timestamp_pb2.Timestamp
    wind: EnvironmentState.Wind
    current: EnvironmentState.Current
    visibility_nm: float
    sea_state_beaufort: int
    def __init__(self, stamp: _Optional[_Union[datetime.datetime, _timestamp_pb2.Timestamp, _Mapping]] = ..., wind: _Optional[_Union[EnvironmentState.Wind, _Mapping]] = ..., current: _Optional[_Union[EnvironmentState.Current, _Mapping]] = ..., visibility_nm: _Optional[float] = ..., sea_state_beaufort: _Optional[int] = ...) -> None: ...
