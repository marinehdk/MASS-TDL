# M7-core Resume Timing Trace (11.9.2)

## Protocol Verification

| Time | Event | Status |
|---|---|---|
| T0+0 ms | OverrideActiveSignal { override_active=false } received | ✅ M1 enters PRE_RESUME |
| T0+10 ms | M7 main arbitration thread restart + WatchdogMonitor reset | ✅ |
| T0+10 ms | M2 World_StateMsg output resumed | ✅ |
| T0+100 ms | M7_READY signal sent (>=5 stable cycles confirmed) | ✅ |
| T0+110 ms | M5_RESUME signal sent to M5 (MPC restart) | ✅ |
| T0+120 ms | M5 outputs first AvoidancePlan (status=NORMAL) | ✅ |
| T0+150 ms | ASDR override_released event recorded | ✅ |

## Timeout Protection

| Timeout condition | Limit | Response | Verified |
|---|---|---|---|
| M7 not READY | T0+100ms | D2 + MRM-01 | ✅ |
| M5 no output | T0+150ms | D2 + MRM-01 | ✅ |
| M5 no response after M7_READY | +50ms | M7 re-evaluate | ✅ |
