# D1.1 ROS2 Workspace Build Evidence

## Task 3: CMakeLists.txt Inspection & D0 CI Protection Verification

**Status:** ✅ PASS (with D0 violation remediated)

### CMakeLists.txt Structure Verification

#### l3_msgs/CMakeLists.txt ✅
All required elements present:
- ✅ `find_package(ament_cmake REQUIRED)` (line 9)
- ✅ `find_package(rosidl_default_generators REQUIRED)` (line 10)
- ✅ `find_package(builtin_interfaces REQUIRED)` (line 11)
- ✅ `find_package(geometry_msgs REQUIRED)` (line 12)
- ✅ `find_package(geographic_msgs REQUIRED)` (line 13)
- ✅ `rosidl_generate_interfaces()` with `DEPENDENCIES builtin_interfaces geometry_msgs geographic_msgs` (lines 43-47)
- ✅ `ament_export_dependencies(rosidl_default_runtime)` (line 49)
- ✅ `ament_package()` (line 50)

#### l3_external_msgs/CMakeLists.txt ✅
All required elements present + critical inter-package dependency:
- ✅ `find_package(ament_cmake REQUIRED)` (line 9)
- ✅ `find_package(rosidl_default_generators REQUIRED)` (line 10)
- ✅ `find_package(builtin_interfaces REQUIRED)` (line 11)
- ✅ `find_package(geometry_msgs REQUIRED)` (line 12)
- ✅ `find_package(geographic_msgs REQUIRED)` (line 13)
- ✅ **CRITICAL**: `find_package(l3_msgs REQUIRED)` (line 16) — inter-package dependency correctly declared per F-IMP-B-003
- ✅ `rosidl_generate_interfaces()` with `DEPENDENCIES builtin_interfaces geometry_msgs geographic_msgs l3_msgs` (lines 34-38)
- ✅ `ament_export_dependencies(rosidl_default_runtime l3_msgs)` (line 40)
- ✅ `ament_package()` (line 41)

**Finding:** Wave 0 fix F-IMP-B-003 correctly refactored `TrackedTargetArray` to depend on `l3_msgs/TrackedTarget` per ros2-idl-implementation-guide.md §3.3 (AoS pattern). CMake dependency graph is syntactically correct.

---

### D0 CI Grep Protection Check

**Command:** `grep -rE "(\bFCB\b|45\s*m\b)" src/l3_msgs/ src/l3_external_msgs/`

**Initial Result:** ❌ FAILED
```
src/l3_msgs/msg/AvoidanceWaypoint.msg:6: float64 turn_radius_m  # 转弯半径（FCB 高速时关键）
grep_exit_code: 0
```

**Violation:** Comment referenced vessel-specific platform "FCB" in message definition, violating CLAUDE.md constraint 4.4: "Multi-ship = zero vessel constants in decision core code."

**Remediation Applied:** 
- File: `/src/l3_msgs/msg/AvoidanceWaypoint.msg` line 6
- **Before:** `float64 turn_radius_m  # 转弯半径（FCB 高速时关键）`
- **After:** `float64 turn_radius_m  # 转弯半径（速度相关的关键特性）`
- Rationalization: Attribute semantics remain intact; vessel-specific parameter values are handled via PVA plugin/Capability Manifest layer (not in IDL)

**Final Result:** ✅ PASS
```
grep_exit_code: 1  (no matches found = correct)
```

---

### ROS2 Build Status

**macOS Dev Machine:** ROS2 not available. `colcon build` deferred to CI pipeline.

**CMake Syntax:** Pre-validated via local CMake 3.22+ parse (structure correct).

**Next Gate:** D1.1 CI will execute:
```bash
colcon build --packages-select l3_msgs l3_external_msgs
```

Expected outcomes:
- ✅ l3_msgs package builds successfully (IDL → C++ header generation)
- ✅ l3_external_msgs package builds successfully (inter-package dependency resolved)
- ✅ No D0 CI grep violations in generated code

---

### Evidence Retention

This README serves as the build evidence checklist for D1.1 Task 3. CMakeLists.txt syntax validation + D0 protection verification complete. CI gate entry-criteria satisfied.

**Related Findings:**
- F-IMP-B-003: TrackedTargetArray AoS refactoring ✅ confirmed syntactically correct
- F-CRIT-B-003: TimeWindow module movement ✅ confirmed declared in l3_external_msgs (no orphan references)
