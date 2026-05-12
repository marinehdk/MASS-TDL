import datetime

from google.protobuf import timestamp_pb2 as _timestamp_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class RadarMeasurement(_message.Message):
    __slots__ = ("stamp", "polar_targets", "clutter_cardinality")
    class PolarTarget(_message.Message):
        __slots__ = ("range", "bearing", "rcs")
        RANGE_FIELD_NUMBER: _ClassVar[int]
        BEARING_FIELD_NUMBER: _ClassVar[int]
        RCS_FIELD_NUMBER: _ClassVar[int]
        range: float
        bearing: float
        rcs: float
        def __init__(self, range: _Optional[float] = ..., bearing: _Optional[float] = ..., rcs: _Optional[float] = ...) -> None: ...
    STAMP_FIELD_NUMBER: _ClassVar[int]
    POLAR_TARGETS_FIELD_NUMBER: _ClassVar[int]
    CLUTTER_CARDINALITY_FIELD_NUMBER: _ClassVar[int]
    stamp: _timestamp_pb2.Timestamp
    polar_targets: _containers.RepeatedCompositeFieldContainer[RadarMeasurement.PolarTarget]
    clutter_cardinality: int
    def __init__(self, stamp: _Optional[_Union[datetime.datetime, _timestamp_pb2.Timestamp, _Mapping]] = ..., polar_targets: _Optional[_Iterable[_Union[RadarMeasurement.PolarTarget, _Mapping]]] = ..., clutter_cardinality: _Optional[int] = ...) -> None: ...
