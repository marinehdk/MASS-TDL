from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional

DESCRIPTOR: _descriptor.FileDescriptor

class PackMarzipRequest(_message.Message):
    __slots__ = ("run_id",)
    RUN_ID_FIELD_NUMBER: _ClassVar[int]
    run_id: str
    def __init__(self, run_id: _Optional[str] = ...) -> None: ...

class PackMarzipResponse(_message.Message):
    __slots__ = ("download_url", "status")
    DOWNLOAD_URL_FIELD_NUMBER: _ClassVar[int]
    STATUS_FIELD_NUMBER: _ClassVar[int]
    download_url: str
    status: str
    def __init__(self, download_url: _Optional[str] = ..., status: _Optional[str] = ...) -> None: ...

class PostProcessArrowRequest(_message.Message):
    __slots__ = ("run_id",)
    RUN_ID_FIELD_NUMBER: _ClassVar[int]
    run_id: str
    def __init__(self, run_id: _Optional[str] = ...) -> None: ...

class PostProcessArrowResponse(_message.Message):
    __slots__ = ("arrow_path",)
    ARROW_PATH_FIELD_NUMBER: _ClassVar[int]
    arrow_path: str
    def __init__(self, arrow_path: _Optional[str] = ...) -> None: ...
