import datetime

from google.protobuf import timestamp_pb2 as _timestamp_pb2
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class ASDREvent(_message.Message):
    __slots__ = ("stamp", "event_type", "rule_ref", "decision_id", "verdict", "payload_json")
    class Verdict(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        VERDICT_UNSPECIFIED: _ClassVar[ASDREvent.Verdict]
        VERDICT_PASS: _ClassVar[ASDREvent.Verdict]
        VERDICT_RISK: _ClassVar[ASDREvent.Verdict]
        VERDICT_FAIL: _ClassVar[ASDREvent.Verdict]
    VERDICT_UNSPECIFIED: ASDREvent.Verdict
    VERDICT_PASS: ASDREvent.Verdict
    VERDICT_RISK: ASDREvent.Verdict
    VERDICT_FAIL: ASDREvent.Verdict
    STAMP_FIELD_NUMBER: _ClassVar[int]
    EVENT_TYPE_FIELD_NUMBER: _ClassVar[int]
    RULE_REF_FIELD_NUMBER: _ClassVar[int]
    DECISION_ID_FIELD_NUMBER: _ClassVar[int]
    VERDICT_FIELD_NUMBER: _ClassVar[int]
    PAYLOAD_JSON_FIELD_NUMBER: _ClassVar[int]
    stamp: _timestamp_pb2.Timestamp
    event_type: str
    rule_ref: str
    decision_id: str
    verdict: ASDREvent.Verdict
    payload_json: str
    def __init__(self, stamp: _Optional[_Union[datetime.datetime, _timestamp_pb2.Timestamp, _Mapping]] = ..., event_type: _Optional[str] = ..., rule_ref: _Optional[str] = ..., decision_id: _Optional[str] = ..., verdict: _Optional[_Union[ASDREvent.Verdict, str]] = ..., payload_json: _Optional[str] = ...) -> None: ...
