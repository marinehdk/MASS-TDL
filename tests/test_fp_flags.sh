#!/bin/bash
set -e

WORKSPACE_DIR="/Users/marine/.gemini/antigravity/brain/24e902bd-a76b-4c9d-93af-1ea0bc89865b/.system_generated/worktrees/subagent-Task-A5--FP-flags-self-5140dc42"
cd "$WORKSPACE_DIR"

echo "=== Running Floating-Point Determinism Build-Config Assertions ==="

# Assertion 1: Ensure absolutely NO "-ffast-math" exists in src/ or colcon.meta
echo "1. Checking that NO -ffast-math exists anywhere in src/ or colcon.meta..."
if grep -r --include="*.cpp" --include="*.h" --include="*.hpp" --include="CMakeLists.txt" --include="colcon.meta" -q "ffast-math" src/ colcon.meta; then
    echo "❌ ERROR: -ffast-math was found in the codebase!"
    grep -r --include="*.cpp" --include="*.h" --include="*.hpp" --include="CMakeLists.txt" --include="colcon.meta" "ffast-math" src/ colcon.meta
    exit 1
else
    echo "✅ Success: No -ffast-math found."
fi

# Assertion 2: Ensure "-Ofast" does not exist in src/ or colcon.meta (since Ofast enables fast-math)
echo "2. Checking that NO -Ofast exists anywhere in src/ or colcon.meta..."
if grep -r --include="*.cpp" --include="*.h" --include="*.hpp" --include="CMakeLists.txt" --include="colcon.meta" -q "\-Ofast" src/ colcon.meta; then
    echo "❌ ERROR: -Ofast was found in the codebase!"
    grep -r --include="*.cpp" --include="*.h" --include="*.hpp" --include="CMakeLists.txt" --include="colcon.meta" "\-Ofast" src/ colcon.meta
    exit 1
else
    echo "✅ Success: No -Ofast found."
fi

# Assertion 3: Ensure -fno-fast-math and -ffp-contract=off are present in CMakeLists.txt of targets
echo "3. Verifying flag presence in CMakeLists.txt files..."

CMAKE_FILES=(
    "src/sim_workbench/fcb_simulator/CMakeLists.txt"
    "src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt"
    "src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt"
    "src/l3_tdl_kernel/m2_world_model/CMakeLists.txt"
)

for file in "${CMAKE_FILES[@]}"; do
    echo "Checking $file..."
    if [ ! -f "$file" ]; then
        echo "❌ ERROR: File $file does not exist!"
        exit 1
    fi
    if ! grep -q "fno-fast-math" "$file"; then
        echo "❌ ERROR: -fno-fast-math missing in $file"
        exit 1
    fi
    if ! grep -q "ffp-contract=off" "$file"; then
        echo "❌ ERROR: -ffp-contract=off missing in $file"
        exit 1
    fi
    echo "✅ $file contains the required flags."
done

# Assertion 4: Ensure -fno-fast-math and -ffp-contract=off are present in colcon.meta for targets
echo "4. Verifying flag presence in colcon.meta..."
if [ ! -f "colcon.meta" ]; then
    echo "❌ ERROR: colcon.meta does not exist!"
    exit 1
fi

# Check that colcon.meta contains the flags
if ! grep -q "fno-fast-math" colcon.meta; then
    echo "❌ ERROR: -fno-fast-math missing in colcon.meta"
    exit 1
fi
if ! grep -q "ffp-contract=off" colcon.meta; then
    echo "❌ ERROR: -ffp-contract=off missing in colcon.meta"
    exit 1
fi
echo "✅ colcon.meta contains the required flags."

echo "=== All Floating-Point Determinism Assertions PASSED! ==="
exit 0
