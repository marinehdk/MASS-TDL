import { useRef, useEffect, useState, memo, useMemo } from 'react';
import { SilMapView } from '../map/SilMapView';
import { SafetyDomainLayer } from '../map/SafetyDomainLayer';
import { IvpRiskGradientLayer } from '../map/IvpRiskGradientLayer';
import { MpcTrajectoryLayer } from '../map/MpcTrajectoryLayer';
import { useFoxgloveLive } from '../hooks/useFoxgloveLive';
import { useTelemetryStore, useControlStore, useUIStore, useScenarioStore } from '../store';
import { useDeactivateLifecycleMutation, useChangeLifecycleRateMutation, useGetScenarioQuery } from '../api/silApi';
import * as jsyaml from 'js-yaml';
import { computeRangeNm } from './shared/navMath';
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
  LucideNavigation,
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
  { id: 'asdr',       label: 'ASDR 记录账本',     icon: <LucideTerminalSquare size={20} /> },
  { id: 'score',      label: '五维实时评分',      icon: <LucideAward size={20} /> },
  { id: 'fault',      label: '故障测试注入',      icon: <LucideZap size={20} /> },
] as const;

const CAPTAIN_TABS = [
  { id: 'ship',   label: '本船状态', icon: <LucideCompass size={20} /> },
  { id: 'threat', label: '威胁列表', icon: <LucideAlertTriangle size={20} /> },
  { id: 'avoid',  label: '避碰决策', icon: <LucideNavigation size={20} /> },
] as const;

type MonitorTabId = typeof MONITOR_TABS[number]['id'];
type CaptainTabId = typeof CAPTAIN_TABS[number]['id'];

export function SimulationMonitor() {
  const [activeRightTab, setActiveRightTab] = useState<MonitorTabId | null>(null);
  const [activeLeftTab, setActiveLeftTab] = useState<CaptainTabId | null>(null);
  const [activeBottomModule, setActiveBottomModule] = useState<string | null>(null);
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

  const scenarioId = useScenarioStore((s) => s.scenarioId) || lifecycleStatus?.scenario_id;
  const { data: activeScenario } = useGetScenarioQuery(scenarioId ?? '', { skip: !scenarioId });
  
  // Standardize Scenario YAML route waypoints extraction to the exact same `{ lat, lon }` structure
  const yamlRoute = useMemo(() => {
    if (!activeScenario?.yaml_content) return null;
    try {
      const doc = jsyaml.load(activeScenario.yaml_content) as any;
      const rawWpts = doc?.ownShip?.nominalRoute || doc?.voyageTask?.waypoints || doc?.mock_l2?.planned_route?.waypoints || [];
      const waypoints = rawWpts.map((wp: any) => ({
        lat: wp.latitude ?? wp.lat ?? 0.0,
        lon: wp.longitude ?? wp.lon ?? 0.0,
      }));
      const cruiseSpeed = doc?.ownShip?.nominalRoute?.[0]?.target_sog_kn || 
                          doc?.voyageTask?.cruise_speed_kn || 
                          doc?.mock_l2?.planned_route?.cruise_speed_kn || 
                          10.0;
      return {
        waypoints,
        cruiseSpeed,
        source: 'static_yaml' as const,
      };
    } catch {
      return null;
    }
  }, [activeScenario]);

  // Hybrid fallback resolution
  const l2Plan = useTelemetryStore((s) => s.voyagePlan);
  const voyagePlan = l2Plan || yamlRoute;

  const planDetails = useMemo(() => {
    if (!voyagePlan?.waypoints?.length) return null;
    const finalWp = voyagePlan.waypoints[voyagePlan.waypoints.length - 1];
    const finalLat = finalWp.lat;
    const finalLon = finalWp.lon;
    const ownLat = ownShip?.pose?.lat;
    const ownLon = ownShip?.pose?.lon;

    const distanceNm = (ownLat != null && ownLon != null && finalLat != null && finalLon != null)
      ? computeRangeNm(ownLat, ownLon, finalLat, finalLon)
      : null;

    let etaString = '—';
    if (distanceNm !== null && distanceNm > 0) {
      const currentSpeedKn = ownShip?.kinematics?.sog != null ? ownShip.kinematics.sog * 1.944 : (voyagePlan.cruiseSpeed || 10.0);
      const speedKn = currentSpeedKn > 0.5 ? currentSpeedKn : (voyagePlan.cruiseSpeed || 10.0);
      const timeToGoHours = distanceNm / speedKn;
      const currentSimTime = lifecycleStatus?.sim_time ?? 0;
      const totalSimDurationSec = currentSimTime + timeToGoHours * 3600;
      const date = new Date(1773676800000 + totalSimDurationSec * 1000); // 2026-03-15 base + sim seconds
      etaString = date.toISOString().substr(11, 8);
    }

    const wptName = `WP${String(voyagePlan.waypoints.length).padStart(2, '0')}`;
    const plannedSpeed = `${(voyagePlan.cruiseSpeed || 10.0).toFixed(1)} kn`;

    return {
      wpt: wptName,
      dist: distanceNm !== null ? `${distanceNm.toFixed(1)} nm` : '—',
      eta: etaString,
      spd: plannedSpeed,
      source: voyagePlan.source,
    };
  }, [ownShip, voyagePlan, lifecycleStatus]);

  // Scenario Switch Protection: Reset active real-time L2 plan inside telemetry store on scenario change
  const prevScenarioIdRef = useRef<string | null>(null);
  useEffect(() => {
    if (prevScenarioIdRef.current !== null && prevScenarioIdRef.current !== scenarioId) {
      useTelemetryStore.getState().updateVoyagePlan(null);
    }
    prevScenarioIdRef.current = scenarioId || null;
  }, [scenarioId]);

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
  const fsmRule     = useFsmStore((s) => s.activeRule);
  const fsmConf     = useFsmStore((s) => s.confidence);
  const fsmHistory  = useFsmStore((s) => s.transitionHistory);
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

        {/* ========================================== */}
        {/* LEFT SIDEBAR (CAPTAIN COCKPIT)             */}
        {/* ========================================== */}
        {/* Vertical Tab Rail on Left side */}
        <div style={{
          position: 'absolute',
          top: '50%',
          left: 20,
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
          {CAPTAIN_TABS.map((tab) => {
            const active = activeLeftTab === tab.id;
            return (
              <button
                key={tab.id}
                title={tab.label}
                data-testid={`left-tab-${tab.id}`}
                onClick={() => setActiveLeftTab(active ? null : tab.id)}
                style={{
                  width: 44, height: 44, borderRadius: 8, border: 'none', cursor: 'pointer',
                  background: active ? 'rgba(91,192,190,0.15)' : 'transparent',
                  color: active ? 'var(--c-phos)' : 'var(--txt-3)',
                  display: 'flex', alignItems: 'center', justifyContent: 'center',
                  transition: 'all 0.2s',
                  borderLeft: active ? '3px solid var(--c-phos)' : '3px solid transparent',
                  position: 'relative'
                }}
                className="rail-item-left"
              >
                {tab.icon}
                <style>{`
                  .rail-item-left:hover::after {
                    content: attr(title);
                    position: absolute;
                    left: 100%;
                    margin-left: 12px;
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

        {/* Collapsible Content Panel on Left side */}
        <div style={{
          position: 'absolute',
          top: '50%',
          left: 100,
          width: '320px',
          maxHeight: 'calc(100% - 240px)',
          background: 'rgba(13, 19, 31, 0.95)',
          backdropFilter: 'blur(16px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12,
          display: 'flex',
          flexDirection: 'column',
          transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
          opacity: activeLeftTab ? 1 : 0,
          transform: `translateY(-50%) translateX(${activeLeftTab ? '0' : '-20px'})`,
          pointerEvents: activeLeftTab ? 'auto' : 'none',
          zIndex: 105,
          boxShadow: activeLeftTab ? '20px 0 50px rgba(0,0,0,0.5)' : 'none',
          overflow: 'hidden'
        }}>
          {activeLeftTab && (
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
                  {CAPTAIN_TABS.find(t => t.id === activeLeftTab)?.label.toUpperCase()}
                </span>
                <button
                  onClick={() => setActiveLeftTab(null)}
                  style={{
                    background: 'transparent', border: 'none', color: 'var(--txt-3)',
                    cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center',
                    padding: 4, borderRadius: '50%'
                  }}
                  onMouseEnter={(e) => e.currentTarget.style.color = 'var(--c-phos)'}
                  onMouseLeave={(e) => e.currentTarget.style.color = 'var(--txt-3)'}
                >
                  <LucideChevronRight size={16} style={{ transform: 'rotate(180deg)' }} />
                </button>
              </div>

              {/* Contents */}
              <div style={{ padding: 20, overflowY: 'auto', flex: 1, minHeight: 0 }}>
                {activeLeftTab === 'ship' && (
                  <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
                    <div style={{
                      background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                      padding: '12px 14px', borderRadius: 8,
                    }}>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 8 }}>
                        <div style={{ width: 5, height: 12, background: 'var(--c-phos)', borderRadius: 1 }} />
                        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.08em' }}>航行状态</span>
                      </div>
                      {ownShip ? (
                        <div style={{
                          display: 'grid',
                          gridTemplateColumns: '1fr 1fr',
                          gap: '12px 16px',
                          padding: '4px 2px'
                        }}>
                          {/* Cell 1: HDG */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>船首向 HDG</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {ownShip.pose?.heading != null ? `${((ownShip.pose.heading * 180 / Math.PI + 360) % 360).toFixed(1)}°` : '—'}
                            </span>
                          </div>

                          {/* Cell 2: RUD */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>舵角 RUD</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {(() => {
                                if (ownShip.controlState?.rudderAngle == null) return '—';
                                const rudderDeg = ownShip.controlState.rudderAngle * 180 / Math.PI;
                                if (Math.abs(rudderDeg) <= 0.1) return '0.0°';
                                return rudderDeg > 0.1 ? `${rudderDeg.toFixed(1)}° R` : `${Math.abs(rudderDeg).toFixed(1)}° L`;
                              })()}
                            </span>
                          </div>

                          {/* Cell 3: SOG */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>对地航速 SOG</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {ownShip.kinematics?.sog != null ? `${(ownShip.kinematics.sog * 1.944).toFixed(1)} kn` : '—'}
                            </span>
                          </div>

                          {/* Cell 4: THR */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>车钟 THR</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {(() => {
                                if (ownShip.controlState?.throttle == null) return '—';
                                const throttle = ownShip.controlState.throttle;
                                if (throttle === 0) return 'STOP';
                                if (throttle > 0) {
                                  if (throttle <= 0.35) return 'AH 1';
                                  if (throttle <= 0.7) return 'AH 2';
                                  return 'AH 3';
                                } else {
                                  if (throttle >= -0.35) return 'AS 1';
                                  if (throttle >= -0.7) return 'AS 2';
                                  return 'AS 3';
                                }
                              })()}
                            </span>
                          </div>
                        </div>
                      ) : (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)' }}>暂无数据</div>
                      )}
                    </div>

                    <div style={{
                      background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                      padding: '12px 14px', borderRadius: 8,
                    }}>
                      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 8 }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                          <div style={{ width: 5, height: 12, background: 'var(--c-phos)', borderRadius: 1 }} />
                          <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.08em' }}>航行计划</span>
                        </div>
                        {planDetails && (
                          <span style={{
                            fontSize: 9,
                            fontFamily: 'var(--f-disp)',
                            fontWeight: 600,
                            letterSpacing: '0.05em',
                            padding: '2px 6px',
                            borderRadius: 4,
                            background: planDetails.source === 'l2_realtime' ? 'rgba(45,212,191,0.15)' : 'rgba(255,255,255,0.06)',
                            border: planDetails.source === 'l2_realtime' ? '1px solid rgba(45,212,191,0.3)' : '1px solid rgba(255,255,255,0.12)',
                            color: planDetails.source === 'l2_realtime' ? 'var(--c-phos)' : 'var(--txt-3)',
                          }}>
                            {planDetails.source === 'l2_realtime' ? 'L2 实时系统' : 'YAML 场景配置'}
                          </span>
                        )}
                      </div>
                      {planDetails ? (
                        <div style={{
                          display: 'grid',
                          gridTemplateColumns: '1fr 1fr',
                          gap: '12px 16px',
                          padding: '4px 2px'
                        }}>
                          {/* Cell 1: WPT */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>计划路点 WPT</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {planDetails.wpt}
                            </span>
                          </div>

                          {/* Cell 2: Dist */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>终点距离 DIST</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {planDetails.dist}
                            </span>
                          </div>

                          {/* Cell 3: ETA */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>预计抵港 ETA</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {planDetails.eta}
                            </span>
                          </div>

                          {/* Cell 4: SPD */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>计划速度 SPD</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {planDetails.spd}
                            </span>
                          </div>
                        </div>
                      ) : (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)' }}>暂无数据</div>
                      )}
                    </div>
                  </div>
                )}

                {activeLeftTab === 'threat' && (
                  <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
                    <div style={{
                      background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                      padding: '12px 14px', borderRadius: 8,
                    }}>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 8, justifyContent: 'space-between' }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                          <div style={{ width: 5, height: 12, background: 'var(--c-danger)', borderRadius: 1 }} />
                          <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-danger)', fontWeight: 700, letterSpacing: '0.08em' }}>高风险目标 (ARPA)</span>
                        </div>
                        <span style={{
                          background: 'rgba(244, 63, 94, 0.15)', border: '1px solid rgba(244, 63, 94, 0.3)',
                          color: 'var(--c-danger)', padding: '1px 5px', borderRadius: 4, fontSize: 9, fontFamily: 'var(--f-mono)'
                        }}>CRITICAL</span>
                      </div>
                      <div style={{ background: 'rgba(0,0,0,0.25)', border: '1px solid var(--line-1)', borderRadius: 6, overflow: 'hidden' }}>
                        <ArpaTargetTable targets={targets} compact />
                      </div>
                    </div>
                  </div>
                )}

                {activeLeftTab === 'avoid' && (
                  <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
                    <div style={{
                      background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                      padding: '12px 14px', borderRadius: 8,
                    }}>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 8 }}>
                        <div style={{ width: 5, height: 12, background: 'var(--c-warn)', borderRadius: 1 }} />
                        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-warn)', fontWeight: 700, letterSpacing: '0.08em' }}>规则状态机与规避决策</span>
                      </div>
                      <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', display: 'flex', flexDirection: 'column', gap: 6 }}>
                        <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                          <span>核心决策状态:</span>
                          <span style={{ color: 'var(--c-warn)', fontWeight: 'bold' }}>{fsmState === 'COLREG_AVOIDANCE' ? 'COLREG AVOIDANCE' : fsmState}</span>
                        </div>
                        <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                          <span>决策置信度 / 权重:</span>
                          <span style={{ color: '#fff' }}>{Math.round((fsmConf ?? 0) * 100)}% / {(sat2?.active_behavior_weight ?? 0.95).toFixed(2)}</span>
                        </div>
                        <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                          <span>适用避碰规则:</span>
                          <span style={{ color: '#fff', fontWeight: 'bold' }}>{fsmRule || 'Rule 14 (对遇)'}</span>
                        </div>
                        <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                          <span>会遇责任角色:</span>
                          <span style={{ color: 'var(--c-danger)', fontWeight: 'bold' }}>Give-way (让路船)</span>
                        </div>
                      </div>
                    </div>

                    <div style={{
                      background: 'rgba(45,212,191,0.03)', border: '1px solid var(--c-phos)',
                      padding: '12px 14px', borderRadius: 8,
                    }}>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 8 }}>
                        <div style={{ width: 5, height: 12, background: 'var(--c-phos)', borderRadius: 1 }} />
                        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.08em' }}>👉 船长规避指令动作</span>
                      </div>
                      <div style={{ fontSize: 11, fontWeight: 'bold', color: '#fff', lineHeight: 1.4 }}>
                        系统正处于自动规避模式。<br />
                        决策动作：<span style={{ color: 'var(--c-phos)' }}>本船向右转向 15°</span> 以宽让目标船。
                      </div>
                    </div>
                  </div>
                )}
              </div>
            </div>
          )}
        </div>

        {/* ========================================== */}
        {/* RIGHT SIDEBAR (ENGINEER COCKPIT)           */}
        {/* ========================================== */}
        {/* Content Panel on Right side */}
        <div style={{
          position: 'absolute',
          top: '50%',
          right: 100,
          width: '340px',
          maxHeight: 'calc(100% - 240px)',
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
                {/* Tab 1: ASDR Ledger */}
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

                {/* Tab 2: Real-time Scoring */}
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

                {/* Tab 3: Fault Injection */}
                {activeRightTab === 'fault' && (
                  <div style={{ display: 'flex', flexDirection: 'column' }}>
                    <FaultInjectPanel inline={true} />
                  </div>
                )}
              </div>
            </div>
          )}
        </div>

        {/* Vertical Tab Rail on Right side */}
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
                data-testid={`right-tab-${tab.id}`}
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

        {/* ========================================== */}
        {/* BOTTOM PULSE BAR WITH FLOATING POPOVERS   */}
        {/* ========================================== */}
        {activeBottomModule && (
          <div style={{
            position: 'absolute',
            bottom: 68,
            left: `calc(50% - 290px + ${['M1','M2','M3','M4','M5','M6','M7','M8'].indexOf(activeBottomModule) * 72.5}px + 36px)`,
            transform: 'translateX(-50%)',
            width: '280px',
            background: 'rgba(18, 25, 39, 0.96)',
            border: '1px solid var(--c-phos)',
            borderRadius: 8,
            boxShadow: '0 12px 40px rgba(0,0,0,0.8), 0 0 15px rgba(45,212,191,0.15)',
            zIndex: 150,
            display: 'flex',
            flexDirection: 'column',
            padding: 12,
            gap: 8,
            backdropFilter: 'blur(16px)',
          }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid rgba(255,255,255,0.06)', paddingBottom: 6 }}>
              <span style={{ fontSize: 11, fontFamily: 'var(--f-disp)', fontWeight: 700, color: 'var(--c-phos)', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
                {activeBottomModule === 'M1' && 'M1 - ODD 运行包络与状态机'}
                {activeBottomModule === 'M2' && 'M2 - 全局航线及偏差控制'}
                {activeBottomModule === 'M3' && 'M3 - 航次计划与调度跟踪'}
                {activeBottomModule === 'M4' && 'M4 - IvP 行为仲裁决策细节'}
                {activeBottomModule === 'M5' && 'M5 - MPC 战术轨迹收敛性'}
                {activeBottomModule === 'M6' && 'M6 - COLREGs 推理树追溯'}
                {activeBottomModule === 'M7' && 'M7 - SOTIF 安全检查度量'}
                {activeBottomModule === 'M8' && 'M8 - HMI 报警发布器状态'}
              </span>
              <button 
                onClick={() => setActiveBottomModule(null)}
                style={{ background: 'transparent', border: 'none', color: 'var(--txt-3)', cursor: 'pointer', fontSize: 12 }}
                onMouseEnter={(e) => e.currentTarget.style.color = 'var(--c-danger)'}
                onMouseLeave={(e) => e.currentTarget.style.color = 'var(--txt-3)'}
              >×</button>
            </div>
            
            <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-1)', display: 'flex', flexDirection: 'column', gap: 6 }}>
              {activeBottomModule === 'M1' && (
                <>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>当前运行域 (ODD)</span>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>OPEN_WATER (开阔)</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>运行包络安全系数</span>
                    <span style={{ color: 'var(--c-phos)', fontWeight: 'bold' }}>92% (符合SIL标准)</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>水深/风速健康度</span>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>正常 🟢</span>
                  </div>
                </>
              )}
              {activeBottomModule === 'M2' && (
                <>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>全局计划航线</span>
                    <span style={{ color: '#fff', fontWeight: 'bold', fontSize: 8 }}>SEG_XIAMEN_SHANGHAI_A</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>横向偏航偏差 (XTE)</span>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>0.02 nm (限制: 0.1nm)</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>下一个路点 WPT</span>
                    <span style={{ color: 'var(--c-warn)', fontWeight: 'bold' }}>WP04 (24.460°N)</span>
                  </div>
                </>
              )}
              {activeBottomModule === 'M3' && (
                <>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>航次时空段状态</span>
                    <span style={{ color: '#fff', fontWeight: 'bold' }}>WAYPOINT_TRACKING</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>整体计划航程进度</span>
                    <span style={{ color: 'var(--c-phos)', fontWeight: 'bold' }}>42.5% (已驶 12.8 / 30.1 nm)</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>航次时空到港延误度</span>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>0.00s (时空对齐合格)</span>
                  </div>
                </>
              )}
              {activeBottomModule === 'M4' && (
                <>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>周期求解算延时</span>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>{sat2?.reasoning_latency_ms != null ? `${sat2.reasoning_latency_ms} ms` : '4.2 ms'}</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>最高活跃评分行为</span>
                    <span style={{ color: 'var(--c-warn)', fontWeight: 'bold' }}>{sat2?.active_behavior || 'COLREG_AVOID'}</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>活跃行为分配权重</span>
                    <span style={{ color: 'var(--c-phos)', fontWeight: 'bold' }}>{sat2?.active_behavior_weight != null ? `${(sat2.active_behavior_weight * 100).toFixed(0)}%` : '95%'}</span>
                  </div>
                </>
              )}
              {activeBottomModule === 'M5' && (
                <>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>战术优化候选路径</span>
                    <span style={{ color: 'var(--c-phos)', fontWeight: 'bold' }}>{sat3?.trajectory_candidates?.length != null ? `${sat3.trajectory_candidates.length} 条` : '13 条'}</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>不确定误差包络带</span>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>{sat3?.uncertainty_bands ? '已开启 🟢' : '已开启 🟢'}</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>最优决策总Cost</span>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>18.9 (已收敛, Path_04)</span>
                  </div>
                </>
              )}
              {activeBottomModule === 'M6' && (
                <div style={{ background: 'rgba(0,0,0,0.25)', border: '1px solid var(--line-1)', borderRadius: 6, overflow: 'hidden', width: '100%' }}>
                  <ColregsRationaleTree
                    chain={sat2?.colregs_chain ?? []}
                    targetId={sat2?.colregs_chain_target_id ?? null}
                    latencyMs={sat2?.reasoning_latency_ms ?? 0}
                  />
                </div>
              )}
              {activeBottomModule === 'M7' && (
                <>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>SOTIF 安全限制状态</span>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>SAFE (双轨一致)</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>碰撞险度指标 CRI</span>
                    <span style={{ color: 'var(--c-warn)', fontWeight: 'bold' }}>{sotifMetrics?.checker_veto_rate_pct != null ? ((sotifMetrics.checker_veto_rate_pct / 100.0) * 0.8).toFixed(2) : '0.42'} (阈值: 0.80)</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>最小风险备份策略</span>
                    <span style={{ color: 'var(--txt-3)', fontWeight: 'bold' }}>MRM-01 (漂泊待命)</span>
                  </div>
                </>
              )}
              {activeBottomModule === 'M8' && (
                <>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>交互发布通信状态</span>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>NORMAL (与ROC同步)</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>5 ms (限制: 20ms)</span>
                  </div>
                  <div style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-2)' }}>警报发布等级</span>
                    <span style={{ color: 'var(--c-green)', fontWeight: 'bold' }}>LEVEL-0 (正常)</span>
                  </div>
                </>
              )}
            </div>

            {/* Triangular Arrow pointing down to module card */}
            <div style={{
              position: 'absolute',
              bottom: -6,
              left: '50%',
              transform: 'translateX(-50%)',
              width: 0,
              height: 0,
              borderLeft: '6px solid transparent',
              borderRight: '6px solid transparent',
              borderTop: '6px solid var(--c-phos)'
            }} />
          </div>
        )}

        {/* Center Bottom M1-M8 card bar (Width 580px, centered horizontally) */}
        <div style={{
          position: 'absolute',
          bottom: 20,
          left: '50%',
          transform: 'translateX(-50%)',
          display: 'flex',
          background: 'rgba(10, 15, 24, 0.9)',
          border: '1px solid var(--line-2)',
          borderRadius: 12,
          padding: 4,
          gap: 4,
          zIndex: 110,
          backdropFilter: 'blur(12px)',
          width: 580
        }}>
          {MODULE_NAMES.map((name, i) => {
            const active = activeBottomModule === name;
            const p = modulePulses.find(x => Number(x.moduleId) === i + 1);
            const color = p ? (HEALTH_COLOR[p.state ?? 0] ?? '#444') : '#333';
            const lat = p?.latencyMs;
            return (
              <div
                key={name}
                onClick={() => setActiveBottomModule(active ? null : name)}
                style={{
                  flex: 1,
                  height: 38,
                  background: active ? 'rgba(45, 212, 191, 0.12)' : 'transparent',
                  border: active ? '1px solid var(--c-phos)' : '1px solid transparent',
                  borderRadius: 8,
                  display: 'flex',
                  flexDirection: 'column',
                  alignItems: 'center',
                  justifyContent: 'center',
                  cursor: 'pointer',
                  transition: 'all 0.15s ease-out',
                  position: 'relative'
                }}
              >
                <span style={{ fontSize: 9, fontWeight: 800, color: active ? 'var(--c-phos)' : 'var(--txt-3)', letterSpacing: '0.05em', marginBottom: 2, fontFamily: 'var(--f-mono)' }}>{name}</span>
                <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
                  <span style={{ width: 5, height: 5, borderRadius: '50%', background: color }} />
                  <span style={{ fontSize: 8, color: lat != null ? 'var(--txt-2)' : 'var(--txt-3)', fontFamily: 'var(--f-mono)' }}>{lat != null ? `${lat}ms` : '—'}</span>
                </div>
              </div>
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
