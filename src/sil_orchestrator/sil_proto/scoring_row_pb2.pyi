import datetime

from google.protobuf import timestamp_pb2 as _timestamp_pb2
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class ScoringRow(_message.Message):
    __slots__ = ("stamp", "safety", "rule_compliance", "delay", "magnitude", "phase", "plausibility", "total")
    STAMP_FIELD_NUMBER: _ClassVar[int]
    SAFETY_FIELD_NUMBER: _ClassVar[int]
    RULE_COMPLIANCE_FIELD_NUMBER: _ClassVar[int]
    DELAY_FIELD_NUMBER: _ClassVar[int]
    MAGNITUDE_FIELD_NUMBER: _ClassVar[int]
    PHASE_FIELD_NUMBER: _ClassVar[int]
    PLAUSIBILITY_FIELD_NUMBER: _ClassVar[int]
    TOTAL_FIELD_NUMBER: _ClassVar[int]
    stamp: _timestamp_pb2.Timestamp
    safety: float
    rule_compliance: float
    delay: float
    magnitude: float
    phase: float
    plausibility: float
    total: float
    def __init__(self, stamp: _Optional[_Union[datetime.datetime, _timestamp_pb2.Timestamp, _Mapping]] = ..., safety: _Optional[float] = ..., rule_compliance: _Optional[float] = ..., delay: _Optional[float] = ..., magnitude: _Optional[float] = ..., phase: _Optional[float] = ..., plausibility: _Optional[float] = ..., total: _Optional[float] = ...) -> None: ...
