import datetime

from google.protobuf import timestamp_pb2 as _timestamp_pb2
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class FaultEvent(_message.Message):
    __slots__ = ("stamp", "fault_type", "payload_json")
    STAMP_FIELD_NUMBER: _ClassVar[int]
    FAULT_TYPE_FIELD_NUMBER: _ClassVar[int]
    PAYLOAD_JSON_FIELD_NUMBER: _ClassVar[int]
    stamp: _timestamp_pb2.Timestamp
    fault_type: str
    payload_json: str
    def __init__(self, stamp: _Optional[_Union[datetime.datetime, _timestamp_pb2.Timestamp, _Mapping]] = ..., fault_type: _Optional[str] = ..., payload_json: _Optional[str] = ...) -> None: ...
