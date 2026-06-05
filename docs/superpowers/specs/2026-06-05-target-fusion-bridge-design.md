# Target Fusion Bridge Design

## Overview
Introduce a new C++ bridge node (`fusion_target_bridge_node`) to consume the external sensor fusion module's target data (`nmea_interfaces::msg::TrackedTargetArray`) and translate it into our internal `l3_external_msgs::msg::TrackedTargetArray` format. 

Concurrently, upgrade the internal `l3_msgs::msg::TrackedTarget` schema to support richer data dimensions (like size and separate covariance matrices) without disrupting downstream kinematic dependencies (such as speed units).

## Architecture
- **Anti-Corruption Layer**: The bridge node isolates the core tactical layer modules (M2-M6) from third-party or hardware-specific changes.
- **Topic Remapping**: The external topic will be mapped to `/external/tracked_targets`. The bridge subscribes to this and publishes the translated data to `/fusion/tracked_targets`.

## Data Schema Changes

### `l3_msgs/msg/TrackedTarget.msg`
- **[NEW]** `uint32 mmsi` - Essential for deterministic multi-sensor data association in M2.
- **[NEW]** `float64 length`, `float64 beam` - Physical dimensions necessary for M5 asymmetric ship domain limits.
- **[MODIFY]** Remove or deprecate `float64[9] covariance`.
- **[NEW]** `float64[4] position_covariance`, `float64[4] velocity_covariance` - Separation of X/Y spatial and velocity uncertainties to optimize 2D Kalman Filter operations.
- **[UNCHANGED]** `sog_kn` - Internal kinematic logic remains bound to knots to minimize regression risks on safety limits. The bridge converts `sog` (m/s) to knots.

### Bridge Conversion Logic
- **Kinematic Conversion**: `sog` (m/s) * 1.94384 = `sog_kn` (knots).
- **Categorical Mapping**: External classifications (e.g., `vessel_large`, `buoy`) are preserved as strings. M2 will apply categorization rules to collapse them into generic semantic types where appropriate.
- **Covariance Extraction**: The 2x2 covariance matrices from `nmea_interfaces` map directly into the new `float64[4]` fields. 

## Internal Impact & M2 Adaptation
- M2's `WorldStateAggregator` and `CpaTcpaCalculator` must be updated to reference `position_covariance` and `velocity_covariance` instead of the old 9-element array.
- Update relevant M2 unit tests to assert the new covariance fields and physical dimensions.

## Verification
- Unit test the `fusion_target_bridge_node` ensuring 100% conversion accuracy, particularly the `sog` to `sog_kn` math and matrix mappings.
- Ensure the changes pass the rigorous `PATH-D` standard (100% MISRA C++:2023 compliance, <60 line function size, complete test coverage).
