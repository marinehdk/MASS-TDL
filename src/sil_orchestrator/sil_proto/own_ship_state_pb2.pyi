import datetime

from google.protobuf import timestamp_pb2 as _timestamp_pb2
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class OwnShipState(_message.Message):
    __slots__ = ("stamp", "pose", "kinematics", "control_state")
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
        __slots__ = ("sog", "cog", "rot", "u", "v", "r")
        SOG_FIELD_NUMBER: _ClassVar[int]
        COG_FIELD_NUMBER: _ClassVar[int]
        ROT_FIELD_NUMBER: _ClassVar[int]
        U_FIELD_NUMBER: _ClassVar[int]
        V_FIELD_NUMBER: _ClassVar[int]
        R_FIELD_NUMBER: _ClassVar[int]
        sog: float
        cog: float
        rot: float
        u: float
        v: float
        r: float
        def __init__(self, sog: _Optional[float] = ..., cog: _Optional[float] = ..., rot: _Optional[float] = ..., u: _Optional[float] = ..., v: _Optional[float] = ..., r: _Optional[float] = ...) -> None: ...
    class ControlState(_message.Message):
        __slots__ = ("rudder_angle", "throttle")
        RUDDER_ANGLE_FIELD_NUMBER: _ClassVar[int]
        THROTTLE_FIELD_NUMBER: _ClassVar[int]
        rudder_angle: float
        throttle: float
        def __init__(self, rudder_angle: _Optional[float] = ..., throttle: _Optional[float] = ...) -> None: ...
    STAMP_FIELD_NUMBER: _ClassVar[int]
    POSE_FIELD_NUMBER: _ClassVar[int]
    KINEMATICS_FIELD_NUMBER: _ClassVar[int]
    CONTROL_STATE_FIELD_NUMBER: _ClassVar[int]
    stamp: _timestamp_pb2.Timestamp
    pose: OwnShipState.Pose
    kinematics: OwnShipState.Kinematics
    control_state: OwnShipState.ControlState
    def __init__(self, stamp: _Optional[_Union[datetime.datetime, _timestamp_pb2.Timestamp, _Mapping]] = ..., pose: _Optional[_Union[OwnShipState.Pose, _Mapping]] = ..., kinematics: _Optional[_Union[OwnShipState.Kinematics, _Mapping]] = ..., control_state: _Optional[_Union[OwnShipState.ControlState, _Mapping]] = ...) -> None: ...
