import { useRef, useEffect, useState, memo } from 'react';
import { SilMapView } from '../map/SilMapView';
import { SafetyDomainLayer } from '../map/SafetyDomainLayer';
import { IvpRiskGradientLayer } from '../map/IvpRiskGradientLayer';
import { MpcTrajectoryLayer } from '../map/MpcTrajectoryLayer';
import { useFoxgloveLive } from '../hooks/useFoxgloveLive';
import { useTelemetryStore, useControlStore, useUIStore } from '../store';
import { useDeactivateLifecycleMutation, useChangeLifecycleRateMutation } from '../api/silApi';
import { RadarPpiDisplay } from '../map/RadarPpiDisplay';
import { DistanceScale } from '../map/DistanceScale';
import { MapLayerSwitcher } from '../map/MapLayerSwitcher';
import { ArpaTargetTable } from './shared/ArpaTargetTable';
import { ScoringGauges } from './shared/ScoringGauges';
import { TorModal } from './shared/TorModal';
import { FaultInjectPanel } from './shared/FaultInjectPanel';
import { ColregsRationaleTree } from './shared/ColregsRationaleTree';
import { DecisionChainTimingBar } from './shared/DecisionChainTimingBar';
import { SotifMonitorStrip } from './shared/SotifMonitorStrip';
import { useFsmStore } from '../store';
import { useHotkeys } from '../hooks/useHotkeys';
import { FsmStatePanel } from '../components/FsmStatePanel';
import {
  LucidePlay, LucidePause, LucideSquare,
  LucideTerminalSquare, LucideAlertTriangle, LucidePanelLeft, LucidePanelRight,
  LucideCompass, LucideActivity, LucideAward, LucideZap, LucideChevronRight,
} from 'lucide-react';
import type maplibregl from 'maplibre-gl';

const MODULE_NAMES = ['M1', 'M2', 'M3', 'M4', 'M5', 'M6', 'M7', 'M8'];
const HEALTH_COLOR: Record<number, string> = { 1: '#34d399', 2: '#fbbf24', 3: '#f87171' };

function fmtSimTime(secs: number) {
  const m = Math.floor(secs / 60).toString().padStart(2, '0');
  const s = Math.floor(secs % 60).toString().padStart(2, '0');
  return `${m}:${s}`;
}

const FSM_BORDER: Record<string, string> = {
  TRANSIT:           'transparent',
  COLREG_AVOIDANCE:  'transparent',
  TOR:               'var(--c-warn)',
  OVERRIDE:          'transparent',
  MRC:               '#8b0000',
};
const FSM_GLOW: Record<string, string> = {
  TOR: '0 0 0 3px rgba(251,191,36,0.4)',
  MRC: '0 0 0 3px rgba(139,0,0,0.6)',
};

const ModulePulseBar = memo(function ModulePulseBar() {
  const pulses = useTelemetryStore((s) => s.modulePulses);
  const byId: Record<number, typeof pulses[0]> = {};
  for (const p of pulses) { if (p.moduleId != null) byId[Number(p.moduleId)] = p; }

  return (
    <div style={{
      display: 'flex', gap: 2, alignItems: 'center', height: 16, background: '#070C13',
      position: 'absolute', top: 0, left: 0, right: 0, zIndex: 30, justifyContent: 'center',
    }}>
      {MODULE_NAMES.map((name, i) => {
        const p = byId[i + 1];
        const color = p ? (HEALTH_COLOR[p.state ?? 0] ?? '#444') : '#333';
        const lat = p?.latencyMs;
        const tip = p
          ? `${name}: ${p.state === 1 ? 'GREEN' : p.state === 2 ? 'AMBER' : 'RED'} | ${lat ?? '?'}ms`
          : `${name}: no data`;
        return (
          <div key={name} title={tip} style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '0 8px', borderRight: '1px solid #1B2C44' }}>
            <div style={{ width: 6, height: 6, borderRadius: '50%', background: color }} />
            <span style={{ color: '#8A9AAD', fontSize: 9, fontFamily: 'var(--f-mono)' }}>{name}</span>
          </div>
        );
      })}
    </div>
  );
});

function LeftDrawer() {
  const sat2 = useTelemetryStore((s) => s.sat2);
  const targets = useTelemetryStore((s) => s.targets);
  const fsmState = useFsmStore((s) => s.currentState);
  const fsmRule = useFsmStore((s) => s.activeRule);
  const fsmConf = useFsmStore((s) => s.confidence);
  const fsmHistory = useFsmStore((s) => s.transitionHistory);
  const [fsmExpanded, setFsmExpanded] = useState(true);

  return (
    <div style={{
      position: 'absolute', top: 16, left: 0, bottom: 80, width: 300,
      background: 'rgba(7,12,19,0.92)', backdropFilter: 'blur(8px)',
      borderRight: '1px solid var(--line-2)', zIndex: 20, overflowY: 'auto',
    }}>
      <FsmStatePanel
        state={fsmState}
        activeRule={fsmRule}
        confidence={fsmConf}
        history={fsmHistory}
        expanded={fsmExpanded}
        onToggleExpand={() => setFsmExpanded(!fsmExpanded)}
      />

      <div style={{ borderBottom: '1px solid var(--line-2)', padding: '6px 12px' }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
          ① ARPA 目标表
        </span>
      </div>
      <div style={{ maxHeight: 180, overflowY: 'auto' }}>
        <ArpaTargetTable targets={targets} compact />
      </div>

      <div style={{ borderBottom: '1px solid var(--line-2)', borderTop: '1px solid var(--line-2)', padding: '6px 12px', marginTop: 4 }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
          ② M6 决策溯源
        </span>
      </div>
      <ColregsRationaleTree
        chain={sat2?.colregs_chain ?? []}
        targetId={sat2?.colregs_chain_target_id ?? null}
        latencyMs={sat2?.reasoning_latency_ms ?? 0}
      />
    </div>
  );
}

const MONITOR_TABS = [
  { id: 'decision',   label: '决策监控 (M4/M5)',  icon: <LucideCompass size={20} /> },
  { id: 'sotif',      label: 'M7 SOTIF 安全',    icon: <LucideActivity size={20} /> },
  { id: 'asdr',       label: 'ASDR 记录账本',     icon: <LucideTerminalSquare size={20} /> },
  { id: 'score',      label: '五维实时评分',      icon: <LucideAward size={20} /> },
  { id: 'fault',      label: '故障测试注入',      icon: <LucideZap size={20} /> },
] as const;

type MonitorTabId = typeof MONITOR_TABS[number]['id'];

export function SimulationMonitor() {
  const [activeRightTab, setActiveRightTab] = useState<MonitorTabId | null>(null);
  const wsUrl = `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}/foxglove-ws`;
  useFoxgloveLive(wsUrl, true);

  const lifecycleStatus = useTelemetryStore((s) => s.lifecycleStatus);
  const asdrEvents      = useTelemetryStore((s) => s.asdrEvents);
  const wsConnected     = useTelemetryStore((s) => s.wsConnected);
  const ownShip         = useTelemetryStore((s) => s.ownShip);
  const targets         = useTelemetryStore((s) => s.targets);
  const modulePulses    = useTelemetryStore((s) => s.modulePulses);
  const sat2            = useTelemetryStore((s) => s.sat2);
  const sat3            = useTelemetryStore((s) => s.sat3);
  const sotifMetrics    = useTelemetryStore((s) => s.sotifMetrics);
  const scoringRow      = useTelemetryStore((s) => s.scoringRow);
  const isSat2Stale     = useTelemetryStore((s) => s.isSat2Stale);
  const isSat3Stale     = useTelemetryStore((s) => s.isSat3Stale);
  const isSotifMetricsStale = useTelemetryStore((s) => s.isSotifMetricsStale);

  const simRate   = useControlStore((s) => s.simRate);
  const isPaused  = useControlStore((s) => s.isPaused);
  const setSimRate = useControlStore((s) => s.setSimRate);
  const setPaused  = useControlStore((s) => s.setPaused);

  const viewMode       = useUIStore((s) => s.viewMode);
  const setViewMode    = useUIStore((s) => s.setViewMode);
  const leftDrawerOpen  = useUIStore((s) => s.leftDrawerOpen);
  const rightDrawerOpen = useUIStore((s) => s.rightDrawerOpen);
  const toggleLeft     = useUIStore((s) => s.toggleLeftDrawer);
  const toggleRight    = useUIStore((s) => s.toggleRightDrawer);
  const asdrExpanded   = useUIStore((s) => s.asdrLogExpanded);
  const toggleAsdr     = useUIStore((s) => s.toggleAsdrLog);

  const fsmState    = useFsmStore((s) => s.currentState);
  const torRequest  = useFsmStore((s) => s.torRequest);

  const [showFaultModal, setShowFaultModal] = useState(false);
  const [substrate, setSubstrate] = useState<'enc' | 'sat' | 'osm'>('enc');
  const [deactivate] = useDeactivateLifecycleMutation();
  const [changeRate] = useChangeLifecycleRateMutation();
  const autoNavRef = useRef(false);
  const externalMapRef = useRef<maplibregl.Map | null>(null);

  const handlePlay = async () => {
    setPaused(false);
    await changeRate(simRate);
  };

  const handlePause = async () => {
    setPaused(true);
    await changeRate(0.0);
  };

  const handleRateChange = async (rate: number) => {
    setSimRate(rate);
    if (!isPaused) {
      await changeRate(rate);
    }
  };

  useHotkeys({
    onSpace: () => {
      if (isPaused) {
        handlePlay();
      } else {
        handlePause();
      }
    },
    onToggleEngineer: () => {
      setViewMode(viewMode === 'engineer' ? 'captain' : 'engineer');
    },
    onToggleRoc: () => {
      setViewMode(viewMode === 'roc' ? 'captain' : 'roc');
    },
    onTor: () => {
      const fsm = useFsmStore.getState();
      if (fsm.currentState !== 'TOR') {
        fsm.setState('TOR', 'MANUAL_TOR_TRIGGER', simTimeSec);
        fsm.setTorRequest({
          reason: 'Manual Operator Intervention Requested (Collision Hazard)',
          triggeredAtSimTime: simTimeSec,
          tmrDeadlineSimTime: simTimeSec + 60,
          currentSituation: 'Target EVT14A040 CPA < 0.15 NM, own ship on autopilot',
          proposedAction: 'Captain/operator takeover to perform manual avoidance maneuver',
          recommendedMrm: 'MRM-01',
        });
      } else {
        fsm.setState('TRANSIT', 'MANUAL_TOR_CANCEL', simTimeSec);
        fsm.setTorRequest(null);
      }
    },
    onFault: () => {
      setActiveRightTab(activeRightTab === 'fault' ? null : 'fault');
    },
    onMrc: () => {
      const fsm = useFsmStore.getState();
      if (fsm.currentState !== 'MRC') {
        fsm.setState('MRC', 'MANUAL_MRC_TRIGGER', simTimeSec);
        fsm.setTorRequest(null);
      } else {
        fsm.setState('TRANSIT', 'MANUAL_MRC_CANCEL', simTimeSec);
      }
    },
    onArrowLeft:  () => {},
    onArrowRight: () => {},
    onArrowUp:    () => {},
    onArrowDown:  () => {},
    onHandback: () => {
      const fsm = useFsmStore.getState();
      fsm.setState('TRANSIT', 'MANUAL_HANDBACK', simTimeSec);
      fsm.setTorRequest(null);
    },
  });

  async function handleStop() {
    await deactivate();
    window.location.hash = '#/evaluator/latest';
  }

  const simTimeSec = lifecycleStatus?.sim_time ?? 0;
  const lcState    = lifecycleStatus?.current_state;

  useEffect(() => {
    if (lcState === 5 && !autoNavRef.current) {
      autoNavRef.current = true;
      const timer = setTimeout(() => { window.location.hash = '#/evaluator/latest'; }, 1500);
      return () => clearTimeout(timer);
    }
  }, [lcState]);

  // Dev-mode auto-switch to engineer view for E2E testing
  useEffect(() => {
    if (window.location.hash.includes('dev=1')) {
      useUIStore.getState().setViewMode('engineer');
    }
  }, []);

  const borderColor = FSM_BORDER[fsmState] ?? 'transparent';
  const boxShadow   = FSM_GLOW[fsmState] ?? 'none';
  const isEngineer  = viewMode === 'engineer';
  const isRoc       = viewMode === 'roc';

  return (
    <div
      data-testid="simulation-monitor"
      data-fsm={fsmState}
      style={{
        height: '100%', display: 'flex', flexDirection: 'column',
        background: 'var(--bg-0)',
        outline: `3px solid ${borderColor}`, outlineOffset: -3,
        boxShadow,
        transition: 'outline-color 0.3s, box-shadow 0.3s',
      }}
    >
      <div style={{ flex: 1, position: 'relative', overflow: 'hidden' }}>
        <ModulePulseBar />

        <SilMapView
          mapRef={externalMapRef}
          followOwnShip={viewMode === 'captain' || viewMode === 'roc'}
          viewMode={viewMode}
          substrate={substrate}
        />

        <SafetyDomainLayer
          mapRef={externalMapRef}
          ownShip={ownShip}
          visible={true}
        />

        {(isEngineer || viewMode === 'god') && (
          <>
            <MpcTrajectoryLayer
              mapRef={externalMapRef}
              candidates={sat3?.trajectory_candidates ?? []}
              visible={true}
            />
            {ownShip && sat2 && (
              <IvpRiskGradientLayer
                mapRef={externalMapRef}
                ownShip={ownShip}
                contributions={sat2.ivp_contributions}
                activeBehavior={sat2.active_behavior}
                activeBehaviorWeight={sat2.active_behavior_weight}
              />
            )}
          </>
        )}



        {!ownShip && (
          <div style={{
            position: 'absolute', inset: 0, zIndex: 50,
            display: 'flex', flexDirection: 'column',
            alignItems: 'center', justifyContent: 'center',
            background: 'rgba(7,12,19,0.88)', fontFamily: 'var(--f-mono)',
          }}>
            <div style={{ marginBottom: 16, fontSize: 14, color: 'var(--txt-1)', letterSpacing: '0.16em' }}>
              AWAITING TELEMETRY
            </div>
            <div style={{ fontSize: 10, color: wsConnected ? '#2dd4bf' : '#f87171' }}>
              {wsConnected ? '● WS CONNECTED' : '○ WS DISCONNECTED'} · {wsUrl}
            </div>
          </div>
        )}

        <div style={{ position: 'absolute', top: 24, right: 16, zIndex: 15 }}>
          <RadarPpiDisplay ownShip={ownShip} targets={targets} relativeMode={viewMode === 'captain'} />
        </div>

        {/* Removed redundant distance scale horizontal line */}


        {/* Unified M4/M5/M7 status info is aggregated inside the right rail drawer */}

        {(isEngineer || viewMode === 'god') && (
          <>
            <button
              data-testid="left-drawer-toggle"
              onClick={toggleLeft}
              style={{
                position: 'absolute', top: '50%', left: leftDrawerOpen ? 304 : 4,
                transform: 'translateY(-50%)', zIndex: 25,
                background: 'rgba(7,12,19,0.8)', border: '1px solid var(--line-2)',
                color: leftDrawerOpen ? 'var(--c-phos)' : 'var(--txt-3)',
                padding: '6px 4px', cursor: 'pointer', borderRadius: '0 4px 4px 0',
                transition: 'left 0.2s',
              }}
            ><LucidePanelLeft size={14} /></button>
          </>
        )}

        {(isEngineer || viewMode === 'god') && leftDrawerOpen  && <LeftDrawer />}

        {/* TIER 1: Unified collapsible content panel (Floating next to vertical rail) */}
        <div style={{
          position: 'absolute',
          top: '50%',
          right: 100,
          width: '340px',
          maxHeight: 'calc(100% - 240px)', // Safe margin of 120px top and bottom
          background: 'rgba(13, 19, 31, 0.95)',
          backdropFilter: 'blur(16px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12,
          display: 'flex',
          flexDirection: 'column',
          transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
          opacity: activeRightTab ? 1 : 0,
          transform: `translateY(-50%) translateX(${activeRightTab ? '0' : '20px'})`,
          pointerEvents: activeRightTab ? 'auto' : 'none',
          zIndex: 105,
          boxShadow: activeRightTab ? '-20px 0 50px rgba(0,0,0,0.5)' : 'none',
          overflow: 'hidden'
        }}>
          {activeRightTab && (
            <div style={{ display: 'flex', flexDirection: 'column', maxHeight: '100%', overflow: 'hidden', minHeight: 0 }}>
              {/* Header */}
              <div style={{
                padding: '16px 20px', borderBottom: '1px solid var(--line-1)',
                display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                flexShrink: 0
              }}>
                <span style={{
                  fontFamily: 'var(--f-disp)', fontSize: 13, fontWeight: 700,
                  color: 'var(--txt-1)', letterSpacing: '0.15em'
                }}>
                  {MONITOR_TABS.find(t => t.id === activeRightTab)?.label.toUpperCase()}
                </span>
                <button
                  onClick={() => setActiveRightTab(null)}
                  style={{
                    background: 'transparent', border: 'none', color: 'var(--txt-3)',
                    cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center',
                    padding: 4, borderRadius: '50%'
                  }}
                  onMouseEnter={(e) => e.currentTarget.style.color = 'var(--c-phos)'}
                  onMouseLeave={(e) => e.currentTarget.style.color = 'var(--txt-3)'}
                >
                  <LucideChevronRight size={16} />
                </button>
              </div>

              {/* Tab Contents */}
              <div style={{ padding: 20, overflowY: 'auto', flex: 1, minHeight: 0 }}>
                {/* Tab 1: Decision Monitor */}
                {activeRightTab === 'decision' && (
                  <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
                    <div style={{
                      background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                      padding: '12px 14px', borderRadius: 8,
                    }}>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 8 }}>
                        <div style={{ width: 5, height: 12, background: 'var(--c-phos)', borderRadius: 1 }} />
                        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.08em' }}>M4 BEHAVIOR ARBITER</span>
                      </div>
                      {isSat2Stale() ? (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--c-warn)' }}>
                          ⚠️ Waiting for M4 IvP data...
                        </div>
                      ) : sat2 ? (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', display: 'flex', flexDirection: 'column', gap: 4 }}>
                          <div>当前决策: <span style={{ color: 'var(--c-phos)' }}>{sat2.active_behavior ?? '-'}</span></div>
                          <div>置信度/权重: <span style={{ color: 'var(--c-phos)' }}>{(sat2.active_behavior_weight * 100).toFixed(0)}%</span></div>
                          <div>决策时延: <span style={{ color: 'var(--c-phos)' }}>{sat2.reasoning_latency_ms} ms</span></div>
                        </div>
                      ) : (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)' }}>暂无数据</div>
                      )}
                    </div>

                    <div style={{
                      background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                      padding: '12px 14px', borderRadius: 8,
                    }}>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 8 }}>
                        <div style={{ width: 5, height: 12, background: 'var(--c-phos)', borderRadius: 1 }} />
                        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.08em' }}>M5 TACTICAL PLANNER</span>
                      </div>
                      {isSat3Stale() ? (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--c-warn)' }}>
                          ⚠️ Waiting for M5 BC-MPC data...
                        </div>
                      ) : sat3 ? (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', display: 'flex', flexDirection: 'column', gap: 4 }}>
                          <div>轨迹候选数: <span style={{ color: 'var(--c-phos)' }}>{sat3.trajectory_candidates.length} candidates</span></div>
                          <div>不确定带: <span style={{ color: 'var(--c-phos)' }}>{sat3.uncertainty_bands ? '已启用' : '未启用'}</span></div>
                        </div>
                      ) : (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)' }}>暂无数据</div>
                      )}
                    </div>
                  </div>
                )}

                {/* Tab 2: M7 SOTIF */}
                {activeRightTab === 'sotif' && (
                  <div style={{ display: 'flex', flexDirection: 'column' }}>
                    <SotifMonitorStrip 
                      metrics={sotifMetrics} 
                      recommendedMrm={useFsmStore.getState().torRequest?.recommendedMrm} 
                      isStale={isSotifMetricsStale()} 
                    />
                  </div>
                )}

                {/* Tab 3: ASDR Ledger */}
                {activeRightTab === 'asdr' && (
                  <div style={{ display: 'flex', flexDirection: 'column', maxHeight: '100%' }}>
                    <div style={{ flex: 1, overflowY: 'auto', maxHeight: '320px', paddingRight: 4 }}>
                      {[...asdrEvents].reverse().slice(0, 30).map((e, i) => (
                        <div key={i} style={{
                          padding: '4px 8px', fontFamily: 'var(--f-mono)', fontSize: 10,
                          color: 'var(--txt-2)', borderBottom: '1px solid rgba(255,255,255,0.03)',
                        }}>
                          <span style={{ color: '#60a5fa' }}>{e.event_type}</span>
                          {e.rule_ref && <span style={{ color: '#a3e635' }}> [{e.rule_ref}]</span>}
                        </div>
                      ))}
                      {asdrEvents.length === 0 && (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)', textAlign: 'center', padding: '20px 0' }}>
                          暂无 ASDR 事件记录
                        </div>
                      )}
                    </div>
                  </div>
                )}

                {/* Tab 4: Real-time Scoring */}
                {activeRightTab === 'score' && (
                  <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
                    {scoringRow ? (
                      <ScoringGauges visible={true} />
                    ) : (
                      <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)', padding: '20px 0' }}>
                        等待评分系统就绪...
                      </div>
                    )}
                  </div>
                )}

                {/* Tab 5: Fault Injection */}
                {activeRightTab === 'fault' && (
                  <div style={{ display: 'flex', flexDirection: 'column' }}>
                    <FaultInjectPanel inline={true} />
                  </div>
                )}

              </div>
            </div>
          )}
        </div>

        {/* TIER 2: Vertical collapsible sidebar tab rail (Centered on the right side) */}
        <div style={{
          position: 'absolute',
          top: '50%',
          right: 20,
          transform: 'translateY(-50%)',
          width: 64, 
          height: 'fit-content',
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          paddingTop: 16,
          paddingBottom: 16,
          gap: 8,
          background: 'rgba(10, 15, 24, 0.9)',
          border: '1px solid var(--line-2)',
          borderRadius: 12,
          transition: 'all 0.2s',
          zIndex: 110
        }}>
          {MONITOR_TABS.map((tab) => {
            const active = activeRightTab === tab.id;
            return (
              <button 
                key={tab.id} 
                title={tab.label}
                onClick={() => setActiveRightTab(active ? null : tab.id)} 
                style={{
                  width: 44, height: 44, borderRadius: 8, border: 'none', cursor: 'pointer',
                  background: active ? 'rgba(91,192,190,0.15)' : 'transparent',
                  color: active ? 'var(--c-phos)' : 'var(--txt-3)',
                  display: 'flex', alignItems: 'center', justifyContent: 'center',
                  transition: 'all 0.2s',
                  borderRight: active ? '3px solid var(--c-phos)' : '3px solid transparent',
                  position: 'relative'
                }}
                className="rail-item-right"
              >
                {tab.icon}
                <style>{`
                  .rail-item-right:hover::after {
                    content: attr(title);
                    position: absolute;
                    right: 100%;
                    margin-right: 12px;
                    background: #0d131f;
                    color: var(--txt-1);
                    padding: 6px 12px;
                    border-radius: 4px;
                    font-size: 11px;
                    white-space: nowrap;
                    z-index: 1000;
                    border: 1px solid var(--line-2);
                    pointer-events: none;
                    box-shadow: 0 4px 20px rgba(0,0,0,0.5);
                  }
                `}</style>
              </button>
            );
          })}
        </div>

        <TorModal />

        {/* Map layer switcher — bottom-right, above the zoom controls */}
        <div style={{ position: 'absolute', bottom: 68, right: 20, zIndex: 20 }}>
          <MapLayerSwitcher activeLayer={substrate} onLayerChange={setSubstrate} />
        </div>
      </div>

      {isEngineer && <DecisionChainTimingBar pulses={modulePulses} />}

      <div style={{
        height: 48, background: 'var(--bg-1)', borderTop: '1px solid var(--line-2)',
        display: 'flex', alignItems: 'center', padding: '0 24px', gap: 24,
        fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--txt-1)', flexShrink: 0,
      }}>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <button onClick={handlePlay} style={{ background: 'transparent', color: !isPaused ? 'var(--c-phos)' : 'var(--txt-2)', border: 'none', cursor: 'pointer' }}>
            <LucidePlay size={20} />
          </button>
          <button onClick={handlePause} style={{ background: 'transparent', color: isPaused ? 'var(--c-warn)' : 'var(--txt-2)', border: 'none', cursor: 'pointer' }}>
            <LucidePause size={20} />
          </button>
          <button onClick={handleStop} style={{ background: 'transparent', color: 'var(--c-danger)', border: 'none', cursor: 'pointer', marginLeft: 8 }}>
            <LucideSquare size={20} />
          </button>
        </div>

        <div style={{ flex: 1, display: 'flex', alignItems: 'center', gap: 12 }}>
          <input type="range" min="0" max="600" value={simTimeSec} style={{ flex: 1, accentColor: 'var(--c-phos)' }} readOnly />
          <span style={{ color: 'var(--txt-1)' }}>{fmtSimTime(simTimeSec)}</span>
        </div>

        {/* Playback speed selector - Premium Segmented Group */}
        <div style={{
          display: 'flex',
          background: 'var(--bg-2)',
          border: '1px solid var(--line-2)',
          borderRadius: 6,
          padding: 2,
          gap: 2,
        }}>
          {[1, 10, 50].map((r) => {
            const active = simRate === r;
            return (
              <button
                key={r}
                onClick={() => handleRateChange(r)}
                onMouseEnter={(e) => {
                  if (!active) {
                    e.currentTarget.style.color = 'var(--txt-1)';
                    e.currentTarget.style.background = 'rgba(255,255,255,0.03)';
                  }
                }}
                onMouseLeave={(e) => {
                  if (!active) {
                    e.currentTarget.style.color = 'var(--txt-3)';
                    e.currentTarget.style.background = 'transparent';
                  }
                }}
                style={{
                  background: active ? 'rgba(45,212,191,0.15)' : 'transparent',
                  color: active ? 'var(--c-phos)' : 'var(--txt-3)',
                  border: 'none',
                  borderRadius: 4,
                  padding: '4px 12px',
                  fontSize: 11,
                  fontFamily: 'var(--f-mono)',
                  fontWeight: active ? 600 : 400,
                  cursor: 'pointer',
                  transition: 'all 0.15s ease',
                  borderBottom: active ? '1px solid var(--c-phos)' : 'none',
                }}
              >
                {r}x
              </button>
            );
          })}
        </div>

        <div style={{ display: 'flex', gap: 16, borderLeft: '1px solid var(--line-2)', paddingLeft: 24 }}>
          <button onClick={toggleAsdr} style={{ background: 'transparent', color: asdrExpanded ? 'var(--c-phos)' : 'var(--txt-2)', border: 'none', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 6 }}>
            <LucideTerminalSquare size={18} /> ASDR
          </button>
          <button onClick={() => setShowFaultModal(true)} style={{ background: 'transparent', color: 'var(--c-danger)', border: 'none', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 6 }}>
            <LucideAlertTriangle size={18} /> FAULT
          </button>
        </div>

      </div>
    </div>
  );
}
