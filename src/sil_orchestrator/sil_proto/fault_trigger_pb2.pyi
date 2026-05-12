from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class TriggerRequest(_message.Message):
    __slots__ = ("fault_type", "payload_json")
    FAULT_TYPE_FIELD_NUMBER: _ClassVar[int]
    PAYLOAD_JSON_FIELD_NUMBER: _ClassVar[int]
    fault_type: str
    payload_json: str
    def __init__(self, fault_type: _Optional[str] = ..., payload_json: _Optional[str] = ...) -> None: ...

class TriggerResponse(_message.Message):
    __slots__ = ("fault_id",)
    FAULT_ID_FIELD_NUMBER: _ClassVar[int]
    fault_id: str
    def __init__(self, fault_id: _Optional[str] = ...) -> None: ...

class ListFaultsRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class ListFaultsResponse(_message.Message):
    __slots__ = ("active",)
    ACTIVE_FIELD_NUMBER: _ClassVar[int]
    active: _containers.RepeatedCompositeFieldContainer[ActiveFault]
    def __init__(self, active: _Optional[_Iterable[_Union[ActiveFault, _Mapping]]] = ...) -> None: ...

class ActiveFault(_message.Message):
    __slots__ = ("fault_id", "fault_type", "stamp")
    FAULT_ID_FIELD_NUMBER: _ClassVar[int]
    FAULT_TYPE_FIELD_NUMBER: _ClassVar[int]
    STAMP_FIELD_NUMBER: _ClassVar[int]
    fault_id: str
    fault_type: str
    stamp: str
    def __init__(self, fault_id: _Optional[str] = ..., fault_type: _Optional[str] = ..., stamp: _Optional[str] = ...) -> None: ...

class CancelRequest(_message.Message):
    __slots__ = ("fault_id",)
    FAULT_ID_FIELD_NUMBER: _ClassVar[int]
    fault_id: str
    def __init__(self, fault_id: _Optional[str] = ...) -> None: ...

class CancelResponse(_message.Message):
    __slots__ = ("success",)
    SUCCESS_FIELD_NUMBER: _ClassVar[int]
    success: bool
    def __init__(self, success: bool = ...) -> None: ...
