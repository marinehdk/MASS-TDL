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
  useStopRuntimeCoreStackMutation,
  useSwitchRuntimePluginMutation,
  useProbeRuntimeMutation,
  type RuntimeMode,
  type RuntimePluginRoleName,
  type RuntimeVerdict,
} from '../api/silApi';
import { GateSequencer } from './shared/GateSequencer';
import { DiagnosticCanvas } from './shared/DiagnosticCanvas';
import { ActionLogs } from './shared/ActionLogs';
import { RuntimeModeSwitch } from './runtime/RuntimeModeSwitch';
import { CheckCategoryNav, type RuntimeCategory } from './runtime/CheckCategoryNav';
import { CoreServicePanel } from './runtime/CoreServicePanel';
import { PluginRolePanel } from './runtime/PluginRolePanel';
import { RuntimeActionLog } from './runtime/RuntimeActionLog';
import { EvidenceStrip } from './runtime/EvidenceStrip';

const IS_DEV = typeof import.meta !== 'undefined' && (import.meta as any).env?.DEV;

type RuntimeActionEntry = {
  time: string;
  message: string;
  level?: 'info' | 'warn' | 'error';
};

const sectionStyle = (active: boolean) => ({
  border: `1px solid ${active ? 'var(--c-info)' : 'var(--line-1)'}`,
  borderRadius: 6,
  background: 'var(--bg-1)',
  padding: 12,
  display: 'grid',
  gap: 12,
});

const summaryCardStyle = {
  border: '1px solid var(--line-1)',
  borderRadius: 6,
  background: 'var(--bg-1)',
  padding: '10px 12px',
  display: 'grid',
  gap: 5,
};

const gateLabel = (gate: { name?: string; role?: string }) => gate.name ?? gate.role ?? 'unknown';

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
  const [stopRuntimeCoreStack] = useStopRuntimeCoreStackMutation();
  const [switchRuntimePlugin] = useSwitchRuntimePluginMutation();
  const [probeRuntime] = useProbeRuntimeMutation();

  const { gates, verdict, streaming, error, start, abort } = useGateStream(scenarioId, true);
  const [focusedGateId, setFocusedGateId] = useState<number | null>(null);
  const [countdown, setCountdown] = useState(-1);
  const [devSkipReason, setDevSkipReason] = useState('');
  const [lifecycleError, setLifecycleError] = useState('');
  const [transitioning, setTransitioning] = useState(false);
  const [selectedCategory, setSelectedCategory] = useState<RuntimeCategory>('mode');
  const [selectedMode, setSelectedMode] = useState<RuntimeMode>('internal');
  const [actionEntries, setActionEntries] = useState<RuntimeActionEntry[]>([]);
  const [runtimeEvidencePath, setRuntimeEvidencePath] = useState<string | undefined>();
  const [runtimeProbeVerdict, setRuntimeProbeVerdict] = useState<RuntimeVerdict | undefined>();
  const countdownRef = useRef(-1);
  const proceedingRef = useRef(false);
  const modeInitializedRef = useRef(false);

  useEffect(() => {
    if (!modeInitializedRef.current && runtimeSummary?.mode) {
      setSelectedMode(runtimeSummary.mode);
      modeInitializedRef.current = true;
    }
  }, [runtimeSummary?.mode]);

  const appendRuntimeLog = useCallback((message: string, level: RuntimeActionEntry['level'] = 'info') => {
    const time = new Date().toLocaleTimeString('zh-CN', { hour12: false });
    setActionEntries((entries) => [{ time, message, level }, ...entries].slice(0, 30));
  }, []);

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

  const handleStopCoreStack = useCallback(async () => {
    setLifecycleError('');
    try {
      await stopRuntimeCoreStack({ confirm: 'STOP_CORE_STACK' }).unwrap();
      appendRuntimeLog('stop core stack');
      refetchRuntimeSummary();
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      appendRuntimeLog(`stop core stack failed: ${message}`, 'error');
      setLifecycleError(`Runtime stop failed: ${message}`);
    }
  }, [appendRuntimeLog, refetchRuntimeSummary, stopRuntimeCoreStack]);

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
      if (runtimeProbe.verdict !== 'GO') {
        const failed = runtimeProbe.gates.find((gate) => !gate.passed);
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
    try {
      // ROS2 lifecycle requires CONFIGURE → ACTIVATE in sequence.
      // cleanup first so re-runs don't get "already configured" rejection.
      await cleanupLifecycle();
      const cfgResult = await configureLifecycle(scenarioId).unwrap();
      if (!cfgResult.success) {
        setLifecycleError(`Configure failed: ${cfgResult.error || 'unknown error'}`);
        setTransitioning(false);
        proceedingRef.current = false;
        return;
      }
      const actResult = await activateLifecycle().unwrap();
      if (!actResult.success) {
        setLifecycleError(`Activate failed: ${actResult.error || 'unknown error'}`);
        setTransitioning(false);
        proceedingRef.current = false;
        return;
      }
      setLifecycleError('');
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e) {
      setLifecycleError(`Lifecycle launch failed: ${e instanceof Error ? e.message : String(e)}`);
      setTransitioning(false);
      proceedingRef.current = false;
    }
  }, [scenarioId, cleanupLifecycle, configureLifecycle, activateLifecycle, probeRuntime, appendRuntimeLog]);

  const handleAbort = useCallback(async () => {
    abort();
    try { await cleanupLifecycle(); } catch {}
    window.location.hash = '#/scenario';
  }, [abort, cleanupLifecycle]);

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

  // GO countdown timer — separated from side-effect
  useEffect(() => {
    if (verdict !== 'GO') return;
    setCountdown(3);
    countdownRef.current = 3;
    const timer = setInterval(() => {
      countdownRef.current -= 1;
      setCountdown(countdownRef.current);
      if (countdownRef.current <= 0) clearInterval(timer);
    }, 1000);
    return () => clearInterval(timer);
  }, [verdict]);

  // GO path: when countdown reaches 0, trigger proceed
  useEffect(() => {
    if (countdown === 0 && verdict === 'GO') {
      handleProceed();
    }
  }, [countdown, verdict, handleProceed]);

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
  const failedRuntimeGate = runtimeSummary?.gates.find((gate) => !gate.passed);
  const categoryStatus: Record<RuntimeCategory, string> = {
    mode: selectedMode === 'internal' ? 'INTERNAL' : 'INTEGRATION',
    core: `${coreServices.filter((service) => service.status === 'running').length}/${coreServices.length}`,
    plugins: `${activePlugins}/${pluginRoles.length}`,
    ros: failedRuntimeGate ? 'CHECK' : 'OK',
    safety: runtimeSummary?.target?.toUpperCase() ?? 'LOCAL',
    verdict: runtimeProbeVerdict ?? runtimeSummary?.verdict ?? 'IDLE',
  };
  const evidencePath = runtimeEvidencePath ?? runtimeSummary?.evidence_path;
  const evidenceVerdict = runtimeProbeVerdict ?? runtimeSummary?.verdict;

  return (
    <div data-testid="preflight" style={{ display: 'grid', gridTemplateColumns: '300px minmax(0, 1fr) 380px', height: '100%', overflow: 'hidden', background: 'var(--bg-0)' }}>
      <div data-testid="preflight-status" style={{ position: 'absolute', top: 8, right: 8, padding: '4px 12px', background: 'var(--bg-1)', borderRadius: 4, zIndex: 10, fontFamily: 'var(--f-mono)', fontSize: 11, color: verdict === 'GO' ? 'var(--c-stbd)' : verdict === 'NO-GO' ? 'var(--c-danger)' : 'var(--txt-3)' }}>
        {verdict ?? (streaming ? 'RUNNING' : 'IDLE')}
      </div>

      <aside style={{ minHeight: 0, overflow: 'auto', borderRight: '1px solid var(--line-1)', padding: 12, display: 'grid', gridTemplateRows: 'auto minmax(0, 1fr)', gap: 12 }}>
        <CheckCategoryNav selected={selectedCategory} onSelect={setSelectedCategory} status={categoryStatus} />
        <div style={{ minHeight: 0, overflow: 'auto', borderTop: '1px solid var(--line-1)', paddingTop: 12 }}>
          <GateSequencer gates={gates} streaming={streaming} focusedGateId={focusedGateId}
            onGateSelect={setFocusedGateId} verdict={verdict} />
        </div>
      </aside>

      <main style={{ minHeight: 0, overflow: 'auto', padding: 14, display: 'grid', gap: 14, alignContent: 'start' }}>
        <header style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 12, flexWrap: 'wrap' }}>
          <div style={{ display: 'grid', gap: 4 }}>
            <h2 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 18, letterSpacing: 0 }}>仿真检查 · 容器运行台</h2>
            <span style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 11 }}>
              {runtimeSummary?.active_profile ?? 'runtime profile pending'}
            </span>
          </div>
          <RuntimeModeSwitch mode={selectedMode} onChange={setSelectedMode} />
        </header>

        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, minmax(0, 1fr))', gap: 10 }}>
          {[
            ['Mode', categoryStatus.mode],
            ['Core', categoryStatus.core],
            ['Plugins', categoryStatus.plugins],
            ['Verdict', categoryStatus.verdict],
          ].map(([label, value]) => (
            <div key={label} style={summaryCardStyle}>
              <span style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10 }}>{label}</span>
              <strong style={{ color: 'var(--txt-0)', fontFamily: 'var(--f-mono)', fontSize: 14 }}>{value}</strong>
            </div>
          ))}
        </div>

        <EvidenceStrip evidencePath={evidencePath} verdict={evidenceVerdict} />

        <section style={sectionStyle(selectedCategory === 'mode')}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 10 }}>
            <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>运行模式</h3>
            <button type="button" onClick={handleRuntimeProbe}>Probe Runtime</button>
          </div>
          <dl style={{ display: 'grid', gridTemplateColumns: '120px 1fr', gap: '6px 12px', margin: 0, color: 'var(--txt-1)', fontSize: 12 }}>
            <dt style={{ color: 'var(--txt-3)' }}>UI Mode</dt>
            <dd style={{ margin: 0, fontFamily: 'var(--f-mono)' }}>{selectedMode}</dd>
            <dt style={{ color: 'var(--txt-3)' }}>Backend Mode</dt>
            <dd style={{ margin: 0, fontFamily: 'var(--f-mono)' }}>{runtimeSummary?.mode ?? 'unknown'}</dd>
            <dt style={{ color: 'var(--txt-3)' }}>Target</dt>
            <dd style={{ margin: 0, fontFamily: 'var(--f-mono)' }}>{runtimeSummary?.target ?? 'unknown'}</dd>
          </dl>
        </section>

        <section style={sectionStyle(selectedCategory === 'core')}>
          <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>TDL 核心容器状态</h3>
          <CoreServicePanel
            services={coreServices}
            onRestart={handleRestartCoreService}
            onStopCoreStack={handleStopCoreStack}
          />
        </section>

        <section style={sectionStyle(selectedCategory === 'plugins')}>
          <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>外部插件容器状态</h3>
          <div style={{ display: 'grid', gap: 12 }}>
            {pluginRoles.length > 0 ? (
              pluginRoles.map((role) => (
                <PluginRolePanel key={role.role} role={role} onSwitch={handleSwitchPlugin} />
              ))
            ) : (
              <span style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 11 }}>No runtime plugins</span>
            )}
          </div>
        </section>

        <section style={sectionStyle(selectedCategory === 'ros')}>
          <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>ROS2 数据链路</h3>
          <div style={{ display: 'grid', gap: 6, color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', fontSize: 11 }}>
            {pluginRoles.flatMap((role) => role.plugins.map((plugin) => (
              <span key={`${role.role}-${plugin.id}`}>
                {role.role} · {plugin.id} · {plugin.topic_status}
              </span>
            )))}
          </div>
        </section>

        <section style={sectionStyle(selectedCategory === 'safety' || selectedCategory === 'verdict')}>
          <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>安全边界 / 放行结论</h3>
          <div style={{ display: 'grid', gap: 6, color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', fontSize: 11 }}>
            {(runtimeSummary?.gates ?? []).map((gate) => (
              <span key={gateLabel(gate)}>
                {gateLabel(gate)} · {gate.passed ? 'PASS' : 'BLOCK'}
              </span>
            ))}
          </div>
        </section>

        <DiagnosticCanvas focusedGateId={focusedGateId} gates={gates}
          scenarioYaml={scenarioDetail?.yaml_content ?? ''}
          storedYaml={scenarioDetail?.yaml_content ?? ''}
          verdict={verdict} countdown={countdown}
          transitioning={transitioning} />
      </main>

      <div style={{ minHeight: 0, overflow: 'auto', borderLeft: '1px solid var(--line-1)', display: 'grid', alignContent: 'start', gap: 12, paddingBottom: 12 }}>
        <ActionLogs focusedGateId={focusedGateId} gates={gates}
          scenarioId={scenarioId} runId={runId ?? 'unknown'}
          onRerun={start} onAbort={handleAbort} onFixApplied={() => {}}
          isDev={IS_DEV && verdict === 'NO-GO'}
          devSkipReason={devSkipReason}
          onDevSkipReasonChange={setDevSkipReason}
          onDevSkip={handleDevSkip} />
        <div style={{ padding: '0 12px' }}>
          <RuntimeActionLog entries={actionEntries} />
        </div>
      </div>

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
