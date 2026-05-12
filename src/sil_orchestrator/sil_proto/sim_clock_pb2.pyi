from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional

DESCRIPTOR: _descriptor.FileDescriptor

class SetRateRequest(_message.Message):
    __slots__ = ("rate",)
    RATE_FIELD_NUMBER: _ClassVar[int]
    rate: float
    def __init__(self, rate: _Optional[float] = ...) -> None: ...

class SetRateResponse(_message.Message):
    __slots__ = ("success",)
    SUCCESS_FIELD_NUMBER: _ClassVar[int]
    success: bool
    def __init__(self, success: bool = ...) -> None: ...

class GetTimeRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class GetTimeResponse(_message.Message):
    __slots__ = ("sim_time", "wall_time")
    SIM_TIME_FIELD_NUMBER: _ClassVar[int]
    WALL_TIME_FIELD_NUMBER: _ClassVar[int]
    sim_time: float
    wall_time: float
    def __init__(self, sim_time: _Optional[float] = ..., wall_time: _Optional[float] = ...) -> None: ...
