from sil import module_pulse_pb2 as _module_pulse_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class ProbeRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class ProbeResponse(_message.Message):
    __slots__ = ("all_clear", "items")
    ALL_CLEAR_FIELD_NUMBER: _ClassVar[int]
    ITEMS_FIELD_NUMBER: _ClassVar[int]
    all_clear: bool
    items: _containers.RepeatedCompositeFieldContainer[CheckItem]
    def __init__(self, all_clear: bool = ..., items: _Optional[_Iterable[_Union[CheckItem, _Mapping]]] = ...) -> None: ...

class CheckItem(_message.Message):
    __slots__ = ("name", "passed", "detail")
    NAME_FIELD_NUMBER: _ClassVar[int]
    PASSED_FIELD_NUMBER: _ClassVar[int]
    DETAIL_FIELD_NUMBER: _ClassVar[int]
    name: str
    passed: bool
    detail: str
    def __init__(self, name: _Optional[str] = ..., passed: bool = ..., detail: _Optional[str] = ...) -> None: ...

class StatusRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class StatusResponse(_message.Message):
    __slots__ = ("module_pulses",)
    MODULE_PULSES_FIELD_NUMBER: _ClassVar[int]
    module_pulses: _containers.RepeatedCompositeFieldContainer[_module_pulse_pb2.ModulePulse]
    def __init__(self, module_pulses: _Optional[_Iterable[_Union[_module_pulse_pb2.ModulePulse, _Mapping]]] = ...) -> None: ...
