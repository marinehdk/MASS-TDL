import pytest

from external_adapters.ipc import decode_line, encode_payload


def test_encode_payload_returns_newline_terminated_json_bytes():
    payload = {"kind": "ownship", "schema_version": 112, "b": 2, "a": 1}

    encoded = encode_payload(payload)

    assert isinstance(encoded, bytes)
    assert encoded.endswith(b"\n")
    assert decode_line(encoded) == payload


def test_decode_line_accepts_known_kind_object():
    decoded = decode_line(b'{"kind":"targets","targets":[]}\n')

    assert decoded == {"kind": "targets", "targets": []}


@pytest.mark.parametrize("line", [b"[]\n", b'"targets"\n', b"null\n"])
def test_decode_line_rejects_non_object_payload(line):
    with pytest.raises(ValueError, match="object"):
        decode_line(line)


@pytest.mark.parametrize("line", [b'{"targets":[]}\n', b'{"kind":"bogus"}\n'])
def test_decode_line_rejects_missing_or_unknown_kind(line):
    with pytest.raises(ValueError, match="kind"):
        decode_line(line)
