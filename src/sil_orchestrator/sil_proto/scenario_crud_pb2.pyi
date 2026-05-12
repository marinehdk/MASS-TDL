from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class ListRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class ListResponse(_message.Message):
    __slots__ = ("scenarios",)
    SCENARIOS_FIELD_NUMBER: _ClassVar[int]
    scenarios: _containers.RepeatedCompositeFieldContainer[ScenarioSummary]
    def __init__(self, scenarios: _Optional[_Iterable[_Union[ScenarioSummary, _Mapping]]] = ...) -> None: ...

class ScenarioSummary(_message.Message):
    __slots__ = ("id", "name", "encounter_type")
    ID_FIELD_NUMBER: _ClassVar[int]
    NAME_FIELD_NUMBER: _ClassVar[int]
    ENCOUNTER_TYPE_FIELD_NUMBER: _ClassVar[int]
    id: str
    name: str
    encounter_type: str
    def __init__(self, id: _Optional[str] = ..., name: _Optional[str] = ..., encounter_type: _Optional[str] = ...) -> None: ...

class GetRequest(_message.Message):
    __slots__ = ("scenario_id",)
    SCENARIO_ID_FIELD_NUMBER: _ClassVar[int]
    scenario_id: str
    def __init__(self, scenario_id: _Optional[str] = ...) -> None: ...

class GetResponse(_message.Message):
    __slots__ = ("yaml_content", "hash")
    YAML_CONTENT_FIELD_NUMBER: _ClassVar[int]
    HASH_FIELD_NUMBER: _ClassVar[int]
    yaml_content: str
    hash: str
    def __init__(self, yaml_content: _Optional[str] = ..., hash: _Optional[str] = ...) -> None: ...

class CreateRequest(_message.Message):
    __slots__ = ("yaml_content",)
    YAML_CONTENT_FIELD_NUMBER: _ClassVar[int]
    yaml_content: str
    def __init__(self, yaml_content: _Optional[str] = ...) -> None: ...

class CreateResponse(_message.Message):
    __slots__ = ("scenario_id", "hash")
    SCENARIO_ID_FIELD_NUMBER: _ClassVar[int]
    HASH_FIELD_NUMBER: _ClassVar[int]
    scenario_id: str
    hash: str
    def __init__(self, scenario_id: _Optional[str] = ..., hash: _Optional[str] = ...) -> None: ...

class UpdateRequest(_message.Message):
    __slots__ = ("scenario_id", "yaml_content")
    SCENARIO_ID_FIELD_NUMBER: _ClassVar[int]
    YAML_CONTENT_FIELD_NUMBER: _ClassVar[int]
    scenario_id: str
    yaml_content: str
    def __init__(self, scenario_id: _Optional[str] = ..., yaml_content: _Optional[str] = ...) -> None: ...

class UpdateResponse(_message.Message):
    __slots__ = ("hash",)
    HASH_FIELD_NUMBER: _ClassVar[int]
    hash: str
    def __init__(self, hash: _Optional[str] = ...) -> None: ...

class DeleteRequest(_message.Message):
    __slots__ = ("scenario_id",)
    SCENARIO_ID_FIELD_NUMBER: _ClassVar[int]
    scenario_id: str
    def __init__(self, scenario_id: _Optional[str] = ...) -> None: ...

class DeleteResponse(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class ValidateRequest(_message.Message):
    __slots__ = ("yaml_content",)
    YAML_CONTENT_FIELD_NUMBER: _ClassVar[int]
    yaml_content: str
    def __init__(self, yaml_content: _Optional[str] = ...) -> None: ...

class ValidateResponse(_message.Message):
    __slots__ = ("valid", "errors")
    VALID_FIELD_NUMBER: _ClassVar[int]
    ERRORS_FIELD_NUMBER: _ClassVar[int]
    valid: bool
    errors: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, valid: bool = ..., errors: _Optional[_Iterable[str]] = ...) -> None: ...
