from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional

DESCRIPTOR: _descriptor.FileDescriptor

class AISMessage(_message.Message):
    __slots__ = ("mmsi", "sog", "cog", "lat", "lon", "heading", "dropout_flag")
    MMSI_FIELD_NUMBER: _ClassVar[int]
    SOG_FIELD_NUMBER: _ClassVar[int]
    COG_FIELD_NUMBER: _ClassVar[int]
    LAT_FIELD_NUMBER: _ClassVar[int]
    LON_FIELD_NUMBER: _ClassVar[int]
    HEADING_FIELD_NUMBER: _ClassVar[int]
    DROPOUT_FLAG_FIELD_NUMBER: _ClassVar[int]
    mmsi: int
    sog: float
    cog: float
    lat: float
    lon: float
    heading: float
    dropout_flag: bool
    def __init__(self, mmsi: _Optional[int] = ..., sog: _Optional[float] = ..., cog: _Optional[float] = ..., lat: _Optional[float] = ..., lon: _Optional[float] = ..., heading: _Optional[float] = ..., dropout_flag: bool = ...) -> None: ...
