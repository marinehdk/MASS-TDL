import { useState, useEffect, useCallback, useRef } from 'react';
import { useGateStream } from '../hooks/useGateStream';
import { useHotkeys } from '../hooks/useHotkeys';
import { useScenarioStore, useTelemetryStore, useControlStore } from '../store';
import {
  useGetScenarioQuery,
  useConfigureLifecycleMutation,
  useActivateLifecycleMutation,
  useCleanupLifecycleMutation,
  useSkipPreflightMutation,
  useGetRuntimeSummaryQuery,
  useRestartRuntimeCoreServiceMutation,
  useSwitchRuntimePluginMutation,
  useProbeRuntimeMutation,
  useStartEvidenceSessionMutation,
  useFinalizeEvidenceSessionMutation,
  type RuntimeMode,
  type RuntimePluginRole,
  type RuntimePluginRoleName,
  type RuntimeVerdict,
} from '../api/silApi';
import { DiagnosticCanvas } from './shared/DiagnosticCanvas';
import { LiveLogStream } from './shared/LiveLogStream';
import { RuntimeModeSwitch } from './runtime/RuntimeModeSwitch';
import { CheckCategoryNav, type RuntimeCategory } from './runtime/CheckCategoryNav';
import { CoreServicePanel } from './runtime/CoreServicePanel';
import { PluginRolePanel } from './runtime/PluginRolePanel';

const IS_DEV = typeof import.meta !== 'undefined' && (import.meta as any).env?.DEV;

type RuntimeActionEntry = {
  time: string;
  message: string;
  level?: 'info' | 'warn' | 'error';
};

const GATE_FILTER_MAP: Record<number, string> = {
  1: 'foxglove|docker',
  2: 'm7_safety',
  3: 'scenario|odd',
  4: 'scenario|odd',
  5: 'clock|chrony',
  6: 'm7|cgroup',
};

const SAFETY_GATE_LABELS: Record<number, string> = {
  4: 'ODD-场景一致',
  5: '时基严密性验证',
  6: '架构物理隔离',
};

const sectionStyle = (active: boolean) => ({
  border: `1px solid ${active ? 'var(--c-phos)' : 'var(--line-1)'}`,
  borderRadius: 8,
  background: 'rgba(10, 15, 24, 0.92)',
  padding: 14,
  display: 'grid',
  gap: 12,
  boxShadow: active ? '0 0 14px rgba(91, 192, 190, 0.12)' : 'none',
});

const summaryCardStyle = {
  border: '1px solid var(--line-1)',
  borderRadius: 8,
  background: 'rgba(10, 15, 24, 0.86)',
  padding: '10px 12px',
  display: 'grid',
  gap: 5,
};

const compactGridStyle = {
  display: 'grid',
  gridTemplateColumns: 'repeat(3, minmax(0, 1fr))',
  gap: 10,
};

const compactCardStyle = {
  minHeight: 104,
  border: '1px solid var(--line-1)',
  borderRadius: 8,
  background: 'rgba(10, 15, 24, 0.86)',
  padding: 12,
  display: 'grid',
  gap: 7,
  alignContent: 'start',
};

const gateLabel = (gate: { name?: string; role?: string }) => gate.name ?? gate.role ?? 'unknown';

function localizedGateName(label: string) {
  const map: Record<string, string> = {
    'ODD-Scenario Alignment': 'ODD-场景一致',
    'Time Base & Evidence Chain': '时基严密性验证',
    'Doer-Checker Independence': '架构物理隔离',
  };
  return map[label] ?? label;
}

const INTERNAL_TOPIC_ROWS = [
  { key: 'internal-own-ship-state', channel: 'SIL状态', topic: '/sil/own_ship_state', connected: true, hasData: true },
  { key: 'internal-target-vessel-state', channel: 'SIL状态', topic: '/sil/target_vessel_state', connected: true, hasData: true },
  { key: 'internal-module-pulse', channel: 'L3心跳', topic: '/sil/module_pulse', connected: true, hasData: true },
  { key: 'internal-lifecycle-status', channel: '生命周期', topic: '/sil/lifecycle_status', connected: true, hasData: true },
  { key: 'internal-actuator-cmd', channel: 'L4指令', topic: '/sil/actuator_cmd', connected: true, hasData: true },
  { key: 'internal-avoidance-plan', channel: 'M5规划', topic: '/l3/m5/avoidance_plan', connected: true, hasData: true },
];

function modeLabel(mode: RuntimeMode) {
  return mode === 'internal' ? '内测模式' : '集成模式';
}

function verdictText(verdict?: RuntimeVerdict | 'IDLE' | null) {
  if (verdict === 'GO') return '通过';
  if (verdict === 'NO-GO') return '失败';
  return '检查中';
}

function decisionStatusKind(verdict?: 'GO' | 'NO-GO' | null, runtimeVerdict?: RuntimeVerdict) {
  if (verdict === 'GO' && runtimeVerdict === 'GO') return 'passed';
  if (verdict === 'NO-GO' || runtimeVerdict === 'NO-GO') return 'failed';
  return 'checking';
}

function decisionColor(kind: 'passed' | 'failed' | 'checking') {
  if (kind === 'passed') return 'var(--c-stbd)';
  if (kind === 'failed') return 'var(--c-danger)';
  return 'var(--c-warn)';
}

function runtimeGateRelevantToMode(gate: { name?: string; role?: string }, displayMode: RuntimeMode) {
  if (displayMode === 'internal') return gateLabel(gate) === 'core_services_running';
  return true;
}

function topicRows(pluginRoles: RuntimePluginRole[]) {
  return pluginRoles.flatMap((role) =>
    role.plugins.flatMap((plugin) =>
      Object.entries(plugin.required_topics).map(([topic, type]) => ({
        key: `${role.role}-${plugin.id}-${topic}`,
        channel: role.role,
        topic,
        type,
        connected: plugin.topic_status === 'ok',
        hasData: plugin.topic_status === 'ok',
      })),
    ),
  );
}

export function SimulationCheck() {
  const scenarioId = window.location.hash.match(/^#\/check\/([^/?#]+)/)?.[1] ?? null;
  const { runId } = useScenarioStore();
  const { data: scenarioDetail } = useGetScenarioQuery(scenarioId ?? '', { skip: !scenarioId });
  const [configureLifecycle] = useConfigureLifecycleMutation();
  const [activateLifecycle] = useActivateLifecycleMutation();
  const [cleanupLifecycle] = useCleanupLifecycleMutation();
  const [skipPreflight] = useSkipPreflightMutation();
  const { data: runtimeSummary, refetch: refetchRuntimeSummary } = useGetRuntimeSummaryQuery();
  const [restartRuntimeCoreService] = useRestartRuntimeCoreServiceMutation();
  const [switchRuntimePlugin] = useSwitchRuntimePluginMutation();
  const [probeRuntime] = useProbeRuntimeMutation();
  const [startEvidenceSession] = useStartEvidenceSessionMutation();
  const [finalizeEvidenceSession] = useFinalizeEvidenceSessionMutation();

  const { gates, verdict, streaming, error, start, abort } = useGateStream(scenarioId, true);
  const [focusedGateId, setFocusedGateId] = useState<number | null>(null);
  const [countdown, setCountdown] = useState(-1);
  const [devSkipReason, setDevSkipReason] = useState('');
  const [lifecycleError, setLifecycleError] = useState('');
  const [transitioning, setTransitioning] = useState(false);
  const [selectedCategory, setSelectedCategory] = useState<RuntimeCategory>('mode');
  const [, setActionEntries] = useState<RuntimeActionEntry[]>([]);
  const [runtimeEvidencePath, setRuntimeEvidencePath] = useState<string | undefined>();
  const [runtimeProbeVerdict, setRuntimeProbeVerdict] = useState<RuntimeVerdict | undefined>();
  const [displayMode, setDisplayMode] = useState<RuntimeMode>('internal');
  const countdownRef = useRef(-1);
  const proceedingRef = useRef(false);
  const activeEvidenceSessionRef = useRef<string | undefined>();

  const appendRuntimeLog = useCallback((message: string, level: RuntimeActionEntry['level'] = 'info') => {
    const time = new Date().toLocaleTimeString('zh-CN', { hour12: false });
    setActionEntries((entries) => [{ time, message, level }, ...entries].slice(0, 30));
  }, []);

  const handleModeSwitchClick = useCallback((mode: RuntimeMode) => {
    setDisplayMode(mode);
    setSelectedCategory('mode');
    if (mode !== runtimeSummary?.mode) {
      appendRuntimeLog(`display mode: ${mode}; backend remains ${runtimeSummary?.mode ?? 'unknown'}`, 'warn');
    }
  }, [appendRuntimeLog, runtimeSummary?.mode]);

  const handleRuntimeProbe = useCallback(async () => {
    setLifecycleError('');
    try {
      const result = await probeRuntime().unwrap();
      setRuntimeEvidencePath(result.evidence_path);
      setRuntimeProbeVerdict(result.verdict);
      appendRuntimeLog(`probe runtime: ${result.verdict}`, result.verdict === 'GO' ? 'info' : 'warn');
      refetchRuntimeSummary();
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      appendRuntimeLog(`probe runtime failed: ${message}`, 'error');
      setLifecycleError(`Runtime probe failed: ${message}`);
    }
  }, [appendRuntimeLog, probeRuntime, refetchRuntimeSummary]);

  const handleRestartCoreService = useCallback(async (service: string) => {
    setLifecycleError('');
    try {
      await restartRuntimeCoreService(service).unwrap();
      appendRuntimeLog(`restart core service: ${service}`);
      refetchRuntimeSummary();
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      appendRuntimeLog(`restart core service failed: ${service}: ${message}`, 'error');
      setLifecycleError(`Runtime restart failed: ${message}`);
    }
  }, [appendRuntimeLog, refetchRuntimeSummary, restartRuntimeCoreService]);

  const handleSwitchPlugin = useCallback(async (role: RuntimePluginRoleName, pluginId: string) => {
    setLifecycleError('');
    try {
      await switchRuntimePlugin({ role, plugin_id: pluginId }).unwrap();
      appendRuntimeLog(`switch plugin: ${role} -> ${pluginId}`);
      refetchRuntimeSummary();
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      appendRuntimeLog(`switch plugin failed: ${role}: ${message}`, 'error');
      setLifecycleError(`Runtime plugin switch failed: ${message}`);
    }
  }, [appendRuntimeLog, refetchRuntimeSummary, switchRuntimePlugin]);

  // useCallback definitions MUST come before effects that reference them
  const handleProceed = useCallback(async () => {
    if (!scenarioId) return;
    if (proceedingRef.current) return;
    proceedingRef.current = true;
    setTransitioning(true);
    try {
      const runtimeProbe = await probeRuntime().unwrap();
      setRuntimeEvidencePath(runtimeProbe.evidence_path);
      setRuntimeProbeVerdict(runtimeProbe.verdict);
      const failed = runtimeProbe.gates.find((gate) => !gate.passed && runtimeGateRelevantToMode(gate, displayMode));
      if (failed) {
        setLifecycleError(`Runtime gate failed: ${failed ? gateLabel(failed) : 'unknown'}`);
        appendRuntimeLog(`runtime gate blocked GO: ${failed ? gateLabel(failed) : 'unknown'}`, 'warn');
        setTransitioning(false);
        proceedingRef.current = false;
        return;
      }
      appendRuntimeLog('runtime gate allowed GO');
    } catch (e) {
      setLifecycleError(`Runtime probe failed: ${e instanceof Error ? e.message : String(e)}`);
      setTransitioning(false);
      proceedingRef.current = false;
      return;
    }
    // Fresh run: clear residual telemetry/trail from any prior run and reset
    // sim rate to the 1x default (otherwise a previously-selected 10x persists
    // in the control store and the new run plays at 10x from t≈last position).
    useTelemetryStore.getState().reset();
    useControlStore.getState().reset();
    let evidenceSessionId: string | undefined;
    const finalizeEvidenceOnError = async () => {
      if (!evidenceSessionId || !scenarioId) return;
      try {
        await finalizeEvidenceSession({
          sessionId: evidenceSessionId,
          scenario_id: scenarioId,
          status: 'error',
        }).unwrap();
      } catch {}
    };
    try {
      const evidence = await startEvidenceSession({
        source: 'frontend',
        suite: 'frontend',
        scenario_id: scenarioId,
      }).unwrap();
      evidenceSessionId = evidence.session_id;
      // configure() owns reset-to-unconfigured before parameter injection.
      // Calling cleanup here can race secondary node teardown and make the
      // next SetParameters request time out.
      const cfgResult = await configureLifecycle(scenarioId).unwrap();
      if (!cfgResult.success) {
        await finalizeEvidenceOnError();
        setLifecycleError(`Configure failed: ${cfgResult.error || 'unknown error'}`);
        setTransitioning(false);
        proceedingRef.current = false;
        return;
      }
      const actResult = await activateLifecycle().unwrap();
      if (!actResult.success) {
        await finalizeEvidenceOnError();
        setLifecycleError(`Activate failed: ${actResult.error || 'unknown error'}`);
        setTransitioning(false);
        proceedingRef.current = false;
        return;
      }
      activeEvidenceSessionRef.current = evidenceSessionId;
      useTelemetryStore.getState().updateLifecycleStatus({
        scenario_id: scenarioId,
        current_state: 3,
        sim_time: 0,
      });
      setLifecycleError('');
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e) {
      await finalizeEvidenceOnError();
      setLifecycleError(`Lifecycle launch failed: ${e instanceof Error ? e.message : String(e)}`);
      setTransitioning(false);
      proceedingRef.current = false;
    }
  }, [
    scenarioId,
    configureLifecycle,
    activateLifecycle,
    probeRuntime,
    appendRuntimeLog,
    displayMode,
    startEvidenceSession,
    finalizeEvidenceSession,
  ]);

  const handleAbort = useCallback(async () => {
    abort();
    if (activeEvidenceSessionRef.current && scenarioId) {
      try {
        await finalizeEvidenceSession({
          sessionId: activeEvidenceSessionRef.current,
          scenario_id: scenarioId,
          status: 'stopped',
        }).unwrap();
      } catch {}
      activeEvidenceSessionRef.current = undefined;
    }
    try { await cleanupLifecycle(); } catch {}
    window.location.hash = '#/scenario';
  }, [abort, cleanupLifecycle, finalizeEvidenceSession, scenarioId]);

  const handleDevSkip = useCallback(async () => {
    if (!scenarioId || !devSkipReason.trim()) return;
    try {
      await skipPreflight({ scenario_id: scenarioId, reason: devSkipReason }).unwrap();
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e) { console.error('dev skip failed:', e); }
  }, [scenarioId, devSkipReason, skipPreflight]);

  useEffect(() => {
    const lastFail = [...gates].reverse().find(g => !g.passed);
    if (lastFail) setFocusedGateId(lastFail.gate_id);
  }, [gates]);

  useHotkeys({
    onTor: verdict !== 'GO' ? () => start() : undefined,
    onFault: () => handleAbort(),
    onMrc: IS_DEV && verdict === 'NO-GO' ? () => handleDevSkip() : undefined,
  });

  if (!scenarioId) {
    return <div style={{ padding: 40, color: 'var(--c-danger)', fontFamily: 'var(--f-body)' }}>No scenario selected</div>;
  }

  const coreServices = runtimeSummary?.core_services ?? [];
  const pluginRoles = runtimeSummary?.plugin_roles ?? [];
  const activePlugins = pluginRoles.filter((role) => Boolean(role.active_plugin)).length;
  const displayCoreCount = coreServices.length || 4;
  const displayPluginCount = displayMode === 'integration' ? (activePlugins || 3) : 0;
  const failedRuntimeGate = runtimeSummary?.gates.find((gate) => !gate.passed && runtimeGateRelevantToMode(gate, displayMode));
  const backendMode = runtimeSummary?.mode;
  const runtimeVerdict = displayMode === 'internal' && verdict === 'GO'
    ? (failedRuntimeGate ? 'NO-GO' : 'GO')
    : (runtimeProbeVerdict ?? runtimeSummary?.verdict);
  const decisionKind = decisionStatusKind(verdict, runtimeVerdict);
  const decisionAccent = decisionColor(decisionKind);
  const categoryStatus: Record<RuntimeCategory, string> = {
    mode: displayMode === 'internal' ? 'INTERNAL' : 'INTEGRATION',
    core: `${coreServices.filter((service) => service.status === 'running').length}/${coreServices.length}`,
    plugins: `${activePlugins}/${pluginRoles.length}`,
    ros: failedRuntimeGate ? 'CHECK' : 'OK',
    safety: runtimeSummary?.target?.toUpperCase() ?? 'LOCAL',
    verdict: runtimeVerdict ?? 'IDLE',
  };
  const rowsByTopic = displayMode === 'internal' ? INTERNAL_TOPIC_ROWS : topicRows(pluginRoles);
  const safetyCards = [4, 5, 6].map((gateId) => {
    const gate = gates.find((item) => item.gate_id === gateId);
    return {
      key: `preflight-${gateId}`,
      name: localizedGateName(gate?.label ?? SAFETY_GATE_LABELS[gateId]),
      source: `检查点 ${String(gateId).padStart(2, '0')}`,
      status: gate ? (gate.passed ? '通过' : '失败') : '等待',
      passed: gate?.passed === true,
    };
  });
  const nodeFilter = focusedGateId ? GATE_FILTER_MAP[focusedGateId] : undefined;

  return (
    <div data-testid="preflight" style={{ display: 'grid', gridTemplateColumns: '300px minmax(0, 1fr)', height: '100%', overflow: 'hidden', background: 'var(--bg-0)' }}>
      <div data-testid="preflight-status" style={{ position: 'absolute', top: 8, right: 8, padding: '4px 12px', background: 'var(--bg-1)', borderRadius: 4, zIndex: 10, fontFamily: 'var(--f-mono)', fontSize: 11, color: verdict === 'GO' ? 'var(--c-stbd)' : verdict === 'NO-GO' ? 'var(--c-danger)' : 'var(--txt-3)' }}>
        {verdict ?? (streaming ? 'RUNNING' : 'IDLE')}
      </div>

      <aside style={{ minHeight: 0, overflow: 'auto', borderRight: '1px solid var(--line-1)' }}>
        <CheckCategoryNav
          selected={selectedCategory}
          onSelect={setSelectedCategory}
          status={categoryStatus}
          gates={gates}
          streaming={streaming}
          focusedGateId={focusedGateId}
          onGateSelect={setFocusedGateId}
          preflightVerdict={verdict}
          runtimeSummary={runtimeSummary}
          displayMode={displayMode}
        />
      </aside>

      <main
        data-testid="runtime-main"
        style={{
          minHeight: 0,
          overflow: 'hidden',
          padding: 14,
          display: 'grid',
          gridTemplateRows: 'minmax(0, 1fr) minmax(0, 1fr)',
          gap: 14,
        }}
      >
        <section data-testid="runtime-top-panel" style={{ minHeight: 0, overflow: 'auto', display: 'grid', gap: 14, alignContent: 'start' }}>
          <header style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 12, flexWrap: 'wrap', padding: 14, border: '1px solid var(--line-1)', borderRadius: 8, background: 'rgba(10, 15, 24, 0.92)' }}>
            <div style={{ display: 'grid', gap: 4 }}>
              <h2 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 18, letterSpacing: 0 }}>仿真检查 · 容器运行台</h2>
              <span style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 11 }}>
                {runtimeSummary?.active_profile ?? 'runtime profile pending'} · 默认内测
              </span>
            </div>
            <RuntimeModeSwitch mode={displayMode} onChange={handleModeSwitchClick} />
          </header>

          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, minmax(0, 1fr))', gap: 10 }}>
            {[
              ['当前模式', `${modeLabel(displayMode)}`],
              ['内部核心', String(displayCoreCount)],
              ['外部插件', String(displayPluginCount)],
              ['检查结论', verdictText(runtimeVerdict)],
            ].map(([label, value]) => (
              <div key={label} style={summaryCardStyle}>
                <span style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10 }}>{label === '当前模式' ? `${label}：${value}` : `${label}：${value}`}</span>
                <strong style={{ color: 'var(--txt-0)', fontFamily: 'var(--f-mono)', fontSize: 14 }}>{value}</strong>
                {label === '当前模式' && displayMode === 'internal' && (
                  <span style={{ color: 'var(--c-phos)', fontFamily: 'var(--f-mono)', fontSize: 10 }}>默认选中</span>
                )}
              </div>
            ))}
          </div>

          {selectedCategory === 'mode' && (
            <section data-testid="runtime-module-mode" style={sectionStyle(true)}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 10 }}>
              <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>运行模式确认</h3>
              <button type="button" onClick={handleRuntimeProbe}>检查运行时</button>
            </div>
            <dl style={{ display: 'grid', gridTemplateColumns: '140px 1fr', gap: '8px 12px', margin: 0, color: 'var(--txt-1)', fontSize: 12 }}>
              <dt style={{ color: 'var(--txt-3)' }}>当前模式</dt>
              <dd style={{ margin: 0, fontFamily: 'var(--f-mono)' }}>{modeLabel(displayMode)}</dd>
              <dt style={{ color: 'var(--txt-3)' }}>模式说明</dt>
              <dd style={{ margin: 0, fontFamily: 'var(--f-mono)' }}>
                {displayMode === 'internal' ? '本地MOCK，没有集成其他容器' : '真实容器，集成除L3外发布版本'}
              </dd>
              <dt style={{ color: 'var(--txt-3)' }}>后端模式</dt>
              <dd style={{ margin: 0, fontFamily: 'var(--f-mono)' }}>{backendMode ?? 'unknown'}</dd>
              <dt style={{ color: 'var(--txt-3)' }}>证据路径</dt>
              <dd style={{ margin: 0, fontFamily: 'var(--f-mono)', wordBreak: 'break-all' }}>
                {runtimeEvidencePath ?? runtimeSummary?.evidence_path ?? '待生成'}
              </dd>
            </dl>
          </section>
          )}

          {selectedCategory === 'core' && (
            <section data-testid="runtime-module-core" style={sectionStyle(true)}>
            <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>内部核心容器</h3>
            <CoreServicePanel
              services={coreServices}
              onRestart={handleRestartCoreService}
            />
          </section>
          )}

          {selectedCategory === 'plugins' && (
            <section data-testid="runtime-module-plugins" style={sectionStyle(true)}>
            <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>外部核心容器</h3>
            <div style={{ display: 'grid', gap: 12 }}>
              {displayMode === 'internal' ? (
                <span style={{ color: 'var(--txt-2)', fontFamily: 'var(--f-mono)', fontSize: 12 }}>
                  内测模式：外部角色容器默认不接入，当前计数 0。
                </span>
              ) : pluginRoles.length > 0 ? (
                pluginRoles.map((role) => (
                  <PluginRolePanel key={role.role} role={role} onSwitch={handleSwitchPlugin} />
                ))
              ) : (
                <span style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 11 }}>暂无外部插件容器</span>
              )}
            </div>
          </section>
          )}

          {selectedCategory === 'ros' && (
            <section data-testid="runtime-module-ros" style={sectionStyle(true)}>
            <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>ROS2数据链路</h3>
            <div data-testid="runtime-ros-grid" style={compactGridStyle}>
              {rowsByTopic.length > 0 ? rowsByTopic.map((row) => (
                <article key={row.key} data-testid={`ros-topic-card-${row.key}`} style={compactCardStyle}>
                  <span style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10 }}>
                    channel
                  </span>
                  <strong style={{ color: 'var(--txt-0)', fontSize: 13 }}>{row.channel}</strong>
                  <span style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10 }}>
                    topic
                  </span>
                  <span style={{ color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', fontSize: 11, wordBreak: 'break-all' }}>
                    {row.topic}
                  </span>
                  <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', fontFamily: 'var(--f-mono)', fontSize: 10 }}>
                    <span style={{ color: row.connected ? 'var(--c-stbd)' : 'var(--c-warn)' }}>
                      联通：{row.connected ? '是' : '否'}
                    </span>
                    <span style={{ color: row.hasData ? 'var(--c-stbd)' : 'var(--c-warn)' }}>
                      数据：{row.hasData ? '有' : '无'}
                    </span>
                  </div>
                </article>
              )) : (
                <span style={{ color: 'var(--txt-3)' }}>暂无话题合同</span>
              )}
            </div>
          </section>
          )}

          {selectedCategory === 'safety' && (
            <section data-testid="runtime-module-safety" style={sectionStyle(true)}>
            <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>安全边界检查</h3>
            <div data-testid="runtime-safety-grid" style={compactGridStyle}>
              {safetyCards.map((card) => (
                <article key={card.key} data-testid={`safety-check-card-${card.key}`} style={compactCardStyle}>
                  <span style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10 }}>
                    {card.source}
                  </span>
                  <strong style={{ color: 'var(--txt-0)', fontSize: 13 }}>{card.name}</strong>
                  <span style={{ color: card.passed ? 'var(--c-stbd)' : 'var(--c-danger)', fontFamily: 'var(--f-mono)', fontSize: 11 }}>
                    {card.status}
                  </span>
                </article>
              ))}
            </div>
          </section>
          )}

          {selectedCategory === 'verdict' && (
            <section data-testid="runtime-module-verdict" style={sectionStyle(true)}>
            <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>仿真检查结论</h3>
            <DiagnosticCanvas focusedGateId={focusedGateId} gates={gates}
              scenarioYaml={scenarioDetail?.yaml_content ?? ''}
              storedYaml={scenarioDetail?.yaml_content ?? ''}
              verdict={verdict} countdown={countdown}
              transitioning={transitioning} />
          </section>
          )}
        </section>

        <section
          data-testid="runtime-bottom-console"
          style={{
            display: 'grid',
            gridTemplateRows: 'auto minmax(0, 1fr) auto',
            gap: 10,
            minHeight: 0,
            border: '1px solid var(--line-1)',
            borderRadius: 8,
            background: 'rgba(10, 15, 24, 0.92)',
            padding: 12,
          }}
        >
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8 }}>
            <div style={{ width: 4, height: 14, background: 'var(--c-phos)', borderRadius: 2 }} />
            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 15, fontWeight: 700, color: 'var(--txt-1)', letterSpacing: '0.16em' }}>
              实时日志
            </span>
          </div>
          <div
            data-testid="runtime-log-frame"
            style={{
              minHeight: 0,
              overflow: 'auto',
              border: '1px solid var(--line-1)',
              borderRadius: 6,
              background: 'rgba(0,0,0,0.18)',
            }}
          >
            <LiveLogStream nodeFilter={nodeFilter} maxLines={180} />
          </div>
          <div
            data-testid="runtime-bottom-actions"
            style={{
              display: 'grid',
              gridTemplateColumns: 'repeat(3, minmax(0, 1fr))',
              gap: 10,
            }}
          >
            <button type="button" onClick={start} style={{
              minHeight: 42,
              border: '1px solid var(--c-phos)',
              borderRadius: 5,
              background: 'var(--c-phos)',
              color: 'var(--bg-0)',
              fontWeight: 800,
              cursor: 'pointer',
            }}>
              重新检查
            </button>
            <button type="button" onClick={handleAbort} style={{
              minHeight: 42,
              border: '1px solid var(--c-danger)',
              borderRadius: 5,
              background: 'transparent',
              color: 'var(--c-danger)',
              fontWeight: 800,
              cursor: 'pointer',
            }}>
              返回场景
            </button>
            <div data-testid="center-decision-panel" style={{ display: 'grid' }}>
              <button
                type="button"
                onClick={handleProceed}
                disabled={verdict !== 'GO' || transitioning}
                style={{
                  minHeight: 42,
                  border: `1px solid ${verdict === 'GO' ? 'var(--c-stbd)' : 'var(--line-2)'}`,
                  borderRadius: 5,
                  background: verdict === 'GO' ? 'var(--c-stbd)' : 'rgba(255,255,255,0.04)',
                  color: verdict === 'GO' ? 'var(--bg-0)' : 'var(--txt-3)',
                  fontWeight: 800,
                  cursor: verdict === 'GO' && !transitioning ? 'pointer' : 'not-allowed',
                }}
              >
                {transitioning ? '启动中' : verdict === 'GO' ? '人工确认 GO' : '等待预检 GO'}
              </button>
            </div>
          </div>
        </section>
      </main>

      {lifecycleError && (
        <div style={{ position: 'fixed', top: 8, left: '50%', transform: 'translateX(-50%)', zIndex: 101, padding: '10px 20px', background: 'rgba(248,81,73,0.2)', border: '1px solid var(--c-danger)', borderRadius: 6, fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--c-danger)', textAlign: 'center', maxWidth: '80vw' }}>
          {lifecycleError}
        </div>
      )}
      {error && (
        <div style={{ position: 'fixed', top: 8, right: 320, zIndex: 100, padding: '8px 16px', background: 'rgba(248,81,73,0.15)', border: '1px solid var(--c-danger)', borderRadius: 4, fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--c-danger)' }}>
          SSE Error: {error}
        </div>
      )}
    </div>
  );
}
