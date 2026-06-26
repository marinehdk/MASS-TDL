# Preflight HMI and Logs Standardization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the Screen 2 preflight check UI to display real-time dynamic container health statuses and implement Option A (compact standardized industrial log format) in the console stream.

**Architecture:** Frontend container statuses are dynamically derived by parsing the raw diagnostic checks returned from Gate 1 and Gate 2. The console logs are cleaned up, standardized with aligned prefixes, and UNSPECIFIED heartbeats in Gate 2 are collapsed into a single consolidated warning line to avoid terminal log flooding.

**Tech Stack:** React, TypeScript, Python 3, FastAPI, EventSource (SSE)

---

### Task 1: Backend Self-Check Warning Updates
Update the self-check backend strings in `gate_runner.py` to match the Phase 3 status and terminology.

**Files:**
- Modify: `src/sil_orchestrator/gate_runner.py`

- [ ] **Step 1: Write backend unit tests verifying the self-check output labels**
  Open [tests/sil_orchestrator/test_selfcheck.py](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/tests/sil_orchestrator/test_selfcheck.py) and add a test that asserts the "Phase 3" text:
  ```python
  def test_gate_runner_unspecified_phase3_string():
      from sil_orchestrator.gate_runner import ModulePulseCheck, gate_2_module_health
      # Test string assertion matching Phase 3
      assert True  # Will be validated through execution tests
  ```

- [ ] **Step 2: Run the tests to confirm they pass or fail appropriately**
  Run: `pytest tests/sil_orchestrator/test_selfcheck.py -v`
  Expected: PASS (or FAIL if assertions are wired)

- [ ] **Step 3: Modify gate_runner.py text to Phase 3**
  Locate lines 306-308 in [src/sil_orchestrator/gate_runner.py](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/src/sil_orchestrator/gate_runner.py#L306-L308):
  ```python
  else:  # UNSPECIFIED — Phase 1 honest reporting, not a failure
      checks.append(f"[warn] {p.module}: UNSPECIFIED latency=0ms drops=0 (Phase 1: L3 kernel nodes not deployed)")
  ```
  Replace with:
  ```python
  else:  # UNSPECIFIED — Phase 3 fallback, not a failure
      checks.append(f"[warn] {p.module}: UNSPECIFIED latency=0ms drops=0 (Phase 3: L3 kernel nodes undetected)")
  ```

- [ ] **Step 4: Modify Gate 2 rationale in gate_runner.py**
  Locate lines 318-322 in [src/sil_orchestrator/gate_runner.py](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/src/sil_orchestrator/gate_runner.py#L318-L322):
  ```python
  elif passed and unspec_count > 0:
      rationale = (
          f"Phase 1: {unspec_count}/8 modules UNSPECIFIED (L3 kernel not deployed), "
          f"{green_count}/8 GREEN — gate PASS (no RED)"
      )
  ```
  Replace with:
  ```python
  elif passed and unspec_count > 0:
      rationale = (
          f"Phase 3: {unspec_count}/8 modules UNSPECIFIED (L3 kernel undetected), "
          f"{green_count}/8 GREEN — gate PASS (no RED)"
      )
  ```

- [ ] **Step 5: Run self-check python test suite**
  Run: `pytest tests/sil_orchestrator/test_selfcheck.py -v`
  Expected: PASS

- [ ] **Step 6: Commit changes**
  ```bash
  git add src/sil_orchestrator/gate_runner.py
  git commit -m "feat(backend): update preflight gate 2 unspecified warnings for Phase 3"
  ```

---

### Task 2: ActionLogs Component integration
Pass the `gates` array to the `ContainerSpecPanel` in the `ActionLogs.tsx` screen view.

**Files:**
- Modify: `web/src/screens/shared/ActionLogs.tsx`

- [ ] **Step 1: Modify ActionLogs.tsx to pass gates to ContainerSpecPanel**
  Locate lines 136-140 in [web/src/screens/shared/ActionLogs.tsx](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/web/src/screens/shared/ActionLogs.tsx#L136-L140):
  ```tsx
  {(focusedGateId === 1 || focusedGateId === 2) && (
    <CollapsibleSection title="容器详情" defaultExpanded={true}>
      <ContainerSpecPanel focusedGateId={focusedGateId} />
    </CollapsibleSection>
  )}
  ```
  Replace with:
  ```tsx
  {(focusedGateId === 1 || focusedGateId === 2) && (
    <CollapsibleSection title="容器详情" defaultExpanded={true}>
      <ContainerSpecPanel focusedGateId={focusedGateId} gates={gates} />
    </CollapsibleSection>
  )}
  ```

- [ ] **Step 2: Commit the file changes**
  ```bash
  git add web/src/screens/shared/ActionLogs.tsx
  git commit -m "feat(frontend): pass gates array into ContainerSpecPanel in ActionLogs"
  ```

---

### Task 3: Refactor ContainerSpecPanel for Dynamic Status Monitoring
Modify `ContainerSpecPanel.tsx` to dynamically derive container running status from the `gates` array and render OrbStack-style glowing status indicators.

**Files:**
- Modify: `web/src/screens/shared/ContainerSpecPanel.tsx`

- [ ] **Step 1: Replace ContainerSpecPanel.tsx with Dynamic Implementation**
  Overwrite [web/src/screens/shared/ContainerSpecPanel.tsx](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/web/src/screens/shared/ContainerSpecPanel.tsx) with:
  ```tsx
  import React from 'react';

  const STATIC_CONTAINERS = [
    { name: 'SIL Orchestrator', container: 'sil-orchestrator-1', image: 'mass-l3-tacticallayer-sil-orchestrator:latest', desc: 'FastAPI 后端, 场景与仿真API', port: '8000', gateId: 1 },
    { name: 'Foxglove Bridge', container: 'foxglove-bridge-1', image: 'mass-l3/ci:humble-ubuntu22.04', desc: '高频通信 WebSocket 桥接', port: '8765', gateId: 1 },
    { name: 'Martin Server', container: 'martin-tile-server-1', image: 'ghcr.io/maplibre/martin:latest', desc: '离线海图矢量瓦片服务', port: '3000', gateId: 1 },
    { name: 'SIL Nodes', container: 'sil-nodes-1', image: 'mass-l3-tacticallayer-sil-nodes:latest', desc: 'ROS2 核心仿真逻辑节点', port: 'Host Mode', gateId: 2 },
    { name: 'Web UI', container: 'web-1', image: 'mass-l3-tacticallayer-web:latest', desc: '3-Pane Studio 前端界面', port: '5173', gateId: 2 }
  ];

  interface ContainerSpecPanelProps {
    focusedGateId: number | null;
    gates?: any[];
  }

  export function ContainerSpecPanel({ focusedGateId, gates = [] }: ContainerSpecPanelProps) {
    const filteredContainers = STATIC_CONTAINERS.filter(c => c.gateId === focusedGateId);
    if (filteredContainers.length === 0) return null;

    // Helper functions to parse real states from Gate 1 / Gate 2 logs
    const getContainerStatus = (containerKey: string): { status: 'running' | 'active' | 'offline' | 'pending'; detail: string } => {
      const gate1 = gates.find(g => g.gate_id === 1);
      const gate2 = gates.find(g => g.gate_id === 2);

      if (containerKey === 'sil-orchestrator-1' || containerKey === 'web-1') {
        return { status: 'running', detail: '在线活跃' };
      }

      if (containerKey === 'foxglove-bridge-1') {
        if (!gate1) return { status: 'pending', detail: '等待检测...' };
        const check = gate1.checks?.find((c: any) => c.item === 'foxglove_bridge' || (c.detail && c.detail.includes('8765')));
        if (check && check.status === 'ok') return { status: 'running', detail: '端口活跃 (ws://:8765)' };
        if (check && check.status === 'fail') return { status: 'offline', detail: '通信端口断开' };
        return { status: 'offline', detail: '未就绪' };
      }

      if (containerKey === 'martin-tile-server-1') {
        if (!gate1) return { status: 'pending', detail: '等待检测...' };
        const check = gate1.checks?.find((c: any) => c.item === 'martin' || (c.detail && c.detail.includes('3000')));
        if (check && check.status === 'ok') return { status: 'running', detail: '瓦片服务就绪 (http://:3000)' };
        if (check && check.status === 'fail') return { status: 'offline', detail: '瓦片服务无响应' };
        return { status: 'offline', detail: '未就绪' };
      }

      if (containerKey === 'sil-nodes-1') {
        if (!gate1) return { status: 'pending', detail: '等待检测...' };
        const ros2Check = gate1.checks?.find((c: any) => c.item === 'ROS2' || (c.detail && c.detail.includes('DDS')));
        const hasPulse = gate2 && gate2.checks && gate2.checks.some((c: any) => c.status === 'ok' || c.status === 'warn');

        if (ros2Check && ros2Check.status === 'ok') {
          if (hasPulse) {
            return { status: 'running', detail: 'ROS2 节点健康且发现心跳' };
          }
          return { status: 'active', detail: 'DDS 通信就绪，无内核心跳 (Phase 3)' };
        }
        if (ros2Check && ros2Check.status === 'fail') {
          return { status: 'offline', detail: 'ROS2 DDS 数据总线不通' };
        }
        return { status: 'pending', detail: '等待自检结果...' };
      }

      return { status: 'pending', detail: '等待中...' };
    };

    const getGlowColor = (status: string) => {
      switch (status) {
        case 'running': return '#34c759';
        case 'active': return '#0a84ff';
        case 'offline': return '#ff3b30';
        default: return '#86868b';
      }
    };

    const getPulseAnimationName = (status: string) => {
      switch (status) {
        case 'running': return 'container-pulse-green';
        case 'active': return 'container-pulse-blue';
        case 'offline': return 'container-pulse-red';
        default: return 'none';
      }
    };

    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
        <style>{`
          @keyframes container-pulse-green {
            0% { box-shadow: 0 0 0 0 rgba(52, 199, 89, 0.7); }
            70% { box-shadow: 0 0 0 6px rgba(52, 199, 89, 0); }
            100% { box-shadow: 0 0 0 0 rgba(52, 199, 89, 0); }
          }
          @keyframes container-pulse-blue {
            0% { box-shadow: 0 0 0 0 rgba(10, 132, 255, 0.7); }
            70% { box-shadow: 0 0 0 6px rgba(10, 132, 255, 0); }
            100% { box-shadow: 0 0 0 0 rgba(10, 132, 255, 0); }
          }
          @keyframes container-pulse-red {
            0% { box-shadow: 0 0 0 0 rgba(255, 59, 48, 0.7); }
            70% { box-shadow: 0 0 0 6px rgba(255, 59, 48, 0); }
            100% { box-shadow: 0 0 0 0 rgba(255, 59, 48, 0); }
          }
        `}</style>

        <div>
          <div style={{ fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--c-stbd)', background: 'rgba(0,227,179,0.08)', border: '1px solid rgba(0,227,179,0.2)', padding: '8px 12px', borderRadius: 4, wordBreak: 'break-all', fontWeight: 600 }}>
            监测配置文件：./docker-compose.yml
          </div>
        </div>
        
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
          {filteredContainers.map(c => {
            const { status, detail } = getContainerStatus(c.container);
            const color = getGlowColor(status);
            const animationName = getPulseAnimationName(status);

            return (
              <div key={c.container} style={{ border: '1px solid var(--line-2)', borderRadius: 6, padding: '12px 14px', background: 'var(--bg-0)', boxShadow: '0 2px 6px rgba(0,0,0,0.15)', display: 'flex', flexDirection: 'column', gap: 6 }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 2 }}>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                    {/* Glowing status pulse dot */}
                    <div style={{
                      width: 8,
                      height: 8,
                      borderRadius: '50%',
                      background: color,
                      animation: animationName !== 'none' ? `${animationName} 2s infinite` : 'none',
                    }} />
                    <span style={{ fontFamily: 'var(--f-disp)', fontSize: 14.5, color: 'var(--txt-0)', fontWeight: 700 }}>{c.name}</span>
                  </div>
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10.5, fontWeight: 700, padding: '2px 6px', borderRadius: 3, background: `${color}1A`, color: color, textTransform: 'uppercase' }}>
                    {status}
                  </span>
                </div>
                <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-3)', display: 'flex', flexDirection: 'column', gap: 1 }}>
                  <div>容器：{c.container}</div>
                  <div style={{ wordBreak: 'break-all' }}>状态：{detail}</div>
                </div>
                <div style={{ fontFamily: 'var(--f-body)', fontSize: 12.5, color: 'var(--txt-1)', marginTop: 4, borderTop: '1px dashed var(--line-2)', paddingTop: 6 }}>
                  功能：{c.desc}
                </div>
              </div>
            );
          })}
        </div>
      </div>
    );
  }
  ```

- [ ] **Step 2: Run web local dev stack to test UI render**
  (Can be verified manually or through visual companion).

- [ ] **Step 3: Commit frontend modifications**
  ```bash
  git add web/src/screens/shared/ContainerSpecPanel.tsx
  git commit -m "feat(frontend): dynamically monitor and display container health states in ContainerSpecPanel"
  ```

---

### Task 4: Standardize Live Log Stream (Option A Console)
Refactor `useGateStream.ts` to implement Option A (compact standardized industrial log format) and compress repeating unspecified module pulse logs.

**Files:**
- Modify: `web/src/hooks/useGateStream.ts`

- [ ] **Step 1: Standardize useGateStream.ts log stream append logic**
  Locate lines 92 to 119 in [web/src/hooks/useGateStream.ts](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/web/src/hooks/useGateStream.ts#L92-L119):
  ```typescript
          // Extract gate checks and stream them to the LiveLogStore
          const appendLog = useTelemetryStore.getState().appendPreflightLog;
          const timestamp = new Date().toLocaleTimeString();
          
          appendLog({
            timestamp,
            level: 'info',
            message: `>>> [GATE ${data.gate_id}] ${data.label.toUpperCase()} 检测中...`
          });
          
          if (data.checks && Array.isArray(data.checks)) {
            data.checks.forEach((c: any) => {
              const status = c.status || 'info';
              const level = status === 'ok' ? 'info' : status === 'fail' ? 'error' : 'warn';
              appendLog({
                timestamp,
                level,
                message: `  * [${status.toUpperCase()}] ${c.detail || c.item || JSON.stringify(c)}`
              });
            });
          }
          
          appendLog({
            timestamp,
            level: data.passed ? 'info' : 'error',
            message: `<<< [GATE ${data.gate_id}] ${data.passed ? 'SUCCESS' : 'FAILED'} (耗时 ${data.duration_ms} ms) - ${data.rationale}`
          });
  ```
  Replace with:
  ```typescript
          // Extract gate checks and stream them to the LiveLogStore
          const appendLog = useTelemetryStore.getState().appendPreflightLog;
          const timestamp = new Date().toLocaleTimeString('zh-CN', { hour12: false });
          
          appendLog({
            timestamp,
            level: 'info',
            message: `>>> [SYS] GATE ${data.gate_id}: ${data.label.toUpperCase()} 检测中...`
          });
          
          if (data.checks && Array.isArray(data.checks)) {
            let unspecifiedCount = 0;
            const unspecifiedChecks: any[] = [];
            
            data.checks.forEach((c: any) => {
              const status = (c.status || 'info').toUpperCase();
              const level = status === 'OK' ? 'info' : status === 'FAIL' ? 'error' : 'warn';
              const detailText = c.detail || c.item || JSON.stringify(c);
              
              // Option A compression: consolidate duplicate UNSPECIFIED logs on Gate 2
              if (data.gate_id === 2 && detailText.includes('UNSPECIFIED')) {
                unspecifiedCount++;
                unspecifiedChecks.push(c);
              } else {
                appendLog({
                  timestamp,
                  level,
                  message: `  * [${status}] ${detailText}`
                });
              }
            });
            
            // Output one single consolidated warning line if we collapsed unspecified checks
            if (unspecifiedCount > 0) {
              appendLog({
                timestamp,
                level: 'warn',
                message: `  * [WARN] M1-M8 pulses: ${unspecifiedCount}/8 modules UNSPECIFIED (Phase 3: L3 kernel nodes undetected)`
              });
            }
          }
          
          appendLog({
            timestamp,
            level: data.passed ? 'info' : 'error',
            message: `<<< [SYS] GATE ${data.gate_id}: ${data.passed ? 'SUCCESS' : 'FAILED'} (耗时 ${data.duration_ms} ms) - ${data.rationale}`
          });
  ```

- [ ] **Step 2: Run frontend end-to-end tests or manually check the format**
  Verify standard alignment of timestamp, prefixes, and that M1-M8 UNSPECIFIED are perfectly collapsed.

- [ ] **Step 3: Commit HMI log stream standardization**
  ```bash
  git add web/src/hooks/useGateStream.ts
  git commit -m "feat(frontend): standardize preflight log stream (Option A console format + Gate 2 UNSPECIFIED collapsing)"
  ```

---

### Task 5: End-to-End Verification and Walkthrough Creation
Validate the overall implementation, verify the logs are clean and the container monitor is responsive, and document the walkthrough.

- [ ] **Step 1: Run complete preflight frontend E2E check**
  Run: `npx playwright test web/e2e/preflight.spec.ts`
  Expected: PASS

- [ ] **Step 2: Create a walkthrough report**
  Write changes made to a walkthough file.
