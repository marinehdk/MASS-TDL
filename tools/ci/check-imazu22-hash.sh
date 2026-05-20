#!/bin/bash
# CI gate: verify Imazu-22 scenario files match frozen SHA256 manifest
# Uses Python hashlib for cross-platform compatibility (macOS + Linux)
# Exit 0 = all match; Exit 1 = mismatch found; Exit 2 = manifest missing
#
# Manifest: scenarios/.imazu22_sha256_manifest.yaml
# Scenarios: scenarios/IMAZU标准测试/imazu-*.yaml
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST="${REPO_ROOT}/scenarios/.imazu22_sha256_manifest.yaml"
SCENARIOS_DIR="${REPO_ROOT}/scenarios/IMAZU标准测试"

if [[ ! -f "$MANIFEST" ]]; then
  echo "FAIL: Manifest not found at $MANIFEST"
  exit 2
fi

# Cross-platform SHA256 via Python hashlib
_sha256() {
  python3 -c "
import hashlib, sys
with open(sys.argv[1], 'rb') as f:
    print(hashlib.sha256(f.read()).hexdigest())
" "$1"
}

fail=0
file_count=0

# Parse YAML manifest (collection.files map)
while IFS=: read -r filename expected_hash; do
  [[ -z "$filename" || -z "$expected_hash" ]] && continue
  
  # Trim whitespace
  filename=$(echo "$filename" | xargs)
  expected_hash=$(echo "$expected_hash" | xargs)

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
done < <(python3 -c "
import yaml
m = yaml.safe_load(open('$MANIFEST', encoding='utf-8'))
for fname, h in m.get('files', {}).items():
    print(f'{fname}: {h}')
")

if [[ $fail -eq 0 ]]; then
  echo "OK: All ${file_count} Imazu scenario files match frozen manifest"
fi
exit $fail
