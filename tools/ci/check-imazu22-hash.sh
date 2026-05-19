#!/bin/bash
# CI gate: verify Imazu scenario files match MANIFEST.sha256
# Uses Python's hashlib for cross-platform SHA256 (macOS + Linux compatible)
# Exit 0 = all match; Exit 1 = mismatch found; Exit 2 = manifest missing
#
# TODO(salvaged-from-d1.3b.1): Path scenarios/imazu22/ is obsolete — main hosts
# Imazu YAMLs at scenarios/IMAZU标准测试/ under DNV schema v3.0. Regenerate
# MANIFEST.sha256 against the current path before wiring into CI:
#   (cd scenarios/IMAZU标准测试 && sha256sum *.yaml > MANIFEST.sha256)
# Then point MANIFEST/SCENARIOS_DIR below to the new location.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST="${REPO_ROOT}/scenarios/imazu22/MANIFEST.sha256"
SCENARIOS_DIR="${REPO_ROOT}/scenarios/imazu22"

if [[ ! -f "$MANIFEST" ]]; then
  echo "FAIL: MANIFEST.sha256 not found at $MANIFEST"
  exit 2
fi

# Cross-platform SHA256 via Python hashlib
_sha256() {
  python3 -c "
import hashlib, sys
path = sys.argv[1]
with open(path, 'rb') as f:
    print(hashlib.sha256(f.read()).hexdigest())
" "$1"
}

fail=0
file_count=0

while IFS= read -r line; do
  # Skip COLLECTION_SHA256 line and empty lines
  [[ "$line" == COLLECTION_SHA256:* ]] && continue
  [[ -z "$line" ]] && continue

  expected_hash="${line:0:64}"
  filename="${line:66}"

  if [[ -z "$filename" ]]; then
    echo "FAIL: Malformed line in manifest: $line"
    fail=1
    continue
  fi

  filepath="${SCENARIOS_DIR}/${filename}"
  if [[ ! -f "$filepath" ]]; then
    echo "FAIL: File not found: $filepath"
    fail=1
    continue
  fi

  actual_hash=$(_sha256 "$filepath")

  if [[ "$actual_hash" != "$expected_hash" ]]; then
    echo "FAIL: $filename hash mismatch"
    echo "  Expected: $expected_hash"
    echo "  Actual:   $actual_hash"
    fail=1
  fi
  ((file_count++)) || true
done < "$MANIFEST"

if [[ $fail -eq 0 ]]; then
  echo "OK: All ${file_count} Imazu scenario files match MANIFEST.sha256"
fi
exit $fail
