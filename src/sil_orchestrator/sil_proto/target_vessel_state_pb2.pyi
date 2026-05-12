import datetime

from google.protobuf import timestamp_pb2 as _timestamp_pb2
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class TargetVesselState(_message.Message):
    __slots__ = ("stamp", "mmsi", "pose", "kinematics", "ship_type", "mode")
    class ShipType(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        SHIP_TYPE_UNSPECIFIED: _ClassVar[TargetVesselState.ShipType]
        SHIP_TYPE_CARGO: _ClassVar[TargetVesselState.ShipType]
        SHIP_TYPE_TANKER: _ClassVar[TargetVesselState.ShipType]
        SHIP_TYPE_PASSENGER: _ClassVar[TargetVesselState.ShipType]
        SHIP_TYPE_FISHING: _ClassVar[TargetVesselState.ShipType]
        SHIP_TYPE_TUG: _ClassVar[TargetVesselState.ShipType]
        SHIP_TYPE_PLEASURE: _ClassVar[TargetVesselState.ShipType]
    SHIP_TYPE_UNSPECIFIED: TargetVesselState.ShipType
    SHIP_TYPE_CARGO: TargetVesselState.ShipType
    SHIP_TYPE_TANKER: TargetVesselState.ShipType
    SHIP_TYPE_PASSENGER: TargetVesselState.ShipType
    SHIP_TYPE_FISHING: TargetVesselState.ShipType
    SHIP_TYPE_TUG: TargetVesselState.ShipType
    SHIP_TYPE_PLEASURE: TargetVesselState.ShipType
    class TargetMode(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        TARGET_MODE_UNSPECIFIED: _ClassVar[TargetVesselState.TargetMode]
        TARGET_MODE_REPLAY: _ClassVar[TargetVesselState.TargetMode]
        TARGET_MODE_NCDM: _ClassVar[TargetVesselState.TargetMode]
        TARGET_MODE_INTELLIGENT: _ClassVar[TargetVesselState.TargetMode]
    TARGET_MODE_UNSPECIFIED: TargetVesselState.TargetMode
    TARGET_MODE_REPLAY: TargetVesselState.TargetMode
    TARGET_MODE_NCDM: TargetVesselState.TargetMode
    TARGET_MODE_INTELLIGENT: TargetVesselState.TargetMode
    class Pose(_message.Message):
        __slots__ = ("lat", "lon", "heading")
        LAT_FIELD_NUMBER: _ClassVar[int]
        LON_FIELD_NUMBER: _ClassVar[int]
        HEADING_FIELD_NUMBER: _ClassVar[int]
        lat: float
        lon: float
        heading: float
        def __init__(self, lat: _Optional[float] = ..., lon: _Optional[float] = ..., heading: _Optional[float] = ...) -> None: ...
    class Kinematics(_message.Message):
        __slots__ = ("sog", "cog", "rot")
        SOG_FIELD_NUMBER: _ClassVar[int]
        COG_FIELD_NUMBER: _ClassVar[int]
        ROT_FIELD_NUMBER: _ClassVar[int]
        sog: float
        cog: float
        rot: float
        def __init__(self, sog: _Optional[float] = ..., cog: _Optional[float] = ..., rot: _Optional[float] = ...) -> None: ...
    STAMP_FIELD_NUMBER: _ClassVar[int]
    MMSI_FIELD_NUMBER: _ClassVar[int]
    POSE_FIELD_NUMBER: _ClassVar[int]
    KINEMATICS_FIELD_NUMBER: _ClassVar[int]
    SHIP_TYPE_FIELD_NUMBER: _ClassVar[int]
    MODE_FIELD_NUMBER: _ClassVar[int]
    stamp: _timestamp_pb2.Timestamp
    mmsi: int
    pose: TargetVesselState.Pose
    kinematics: TargetVesselState.Kinematics
    ship_type: TargetVesselState.ShipType
    mode: TargetVesselState.TargetMode
    def __init__(self, stamp: _Optional[_Union[datetime.datetime, _timestamp_pb2.Timestamp, _Mapping]] = ..., mmsi: _Optional[int] = ..., pose: _Optional[_Union[TargetVesselState.Pose, _Mapping]] = ..., kinematics: _Optional[_Union[TargetVesselState.Kinematics, _Mapping]] = ..., ship_type: _Optional[_Union[TargetVesselState.ShipType, str]] = ..., mode: _Optional[_Union[TargetVesselState.TargetMode, str]] = ...) -> None: ...
