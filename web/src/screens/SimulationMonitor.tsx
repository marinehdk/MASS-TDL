import { useRef, useEffect, useState, memo } from 'react';
import { SilMapView } from '../map/SilMapView';
import { SafetyDomainLayer } from '../map/SafetyDomainLayer';
import { IvpRiskGradientLayer } from '../map/IvpRiskGradientLayer';
import { MpcTrajectoryLayer } from '../map/MpcTrajectoryLayer';
import { useFoxgloveLive } from '../hooks/useFoxgloveLive';
import { useDemoTelemetry } from '../hooks/useDemoTelemetry';
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
import {
  LucidePlay, LucidePause, LucideSquare,
  LucideTerminalSquare, LucideAlertTriangle, LucidePanelLeft, LucidePanelRight,
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

  return (
    <div style={{
      position: 'absolute', top: 16, left: 0, bottom: 80, width: 300,
      background: 'rgba(7,12,19,0.92)', backdropFilter: 'blur(8px)',
      borderRight: '1px solid var(--line-2)', zIndex: 20, overflowY: 'auto',
    }}>
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

function RightDrawer() {
  const sotifMetrics = useTelemetryStore((s) => s.sotifMetrics);
  const asdrEvents   = useTelemetryStore((s) => s.asdrEvents);
  const scoringRow   = useTelemetryStore((s) => s.scoringRow);

  return (
    <div style={{
      position: 'absolute', top: 16, right: 0, bottom: 80, width: 280,
      background: 'rgba(7,12,19,0.92)', backdropFilter: 'blur(8px)',
      borderLeft: '1px solid var(--line-2)', zIndex: 20, overflowY: 'auto',
    }}>
      <SotifMonitorStrip metrics={sotifMetrics} recommendedMrm={useFsmStore.getState().torRequest?.recommendedMrm} />

      <div style={{ borderTop: '1px solid var(--line-2)', padding: '6px 12px' }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
          ④ ASDR Ledger
        </span>
      </div>
      <div style={{ maxHeight: 140, overflowY: 'auto', padding: '4px 0' }}>
        {[...asdrEvents].reverse().slice(0, 30).map((e, i) => (
          <div key={i} style={{
            padding: '2px 12px', fontFamily: 'var(--f-mono)', fontSize: 9,
            color: 'var(--txt-2)', borderBottom: '1px solid rgba(255,255,255,0.03)',
          }}>
            <span style={{ color: '#60a5fa' }}>{e.event_type}</span>
            {e.rule_ref && <span style={{ color: '#a3e635' }}> [{e.rule_ref}]</span>}
          </div>
        ))}
      </div>

      {scoringRow && (
        <div style={{ borderTop: '1px solid var(--line-2)', padding: '6px 12px' }}>
          <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
            ⑤ 实时评分
          </span>
          <ScoringGauges visible={true} />
        </div>
      )}
    </div>
  );
}

export function SimulationMonitor() {
  const [useDemo, setUseDemo] = useState(false);
  const wsUrl = `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}/foxglove-ws`;
  useFoxgloveLive(wsUrl);
  useDemoTelemetry(useDemo);

  const lifecycleStatus = useTelemetryStore((s) => s.lifecycleStatus);
  const asdrEvents      = useTelemetryStore((s) => s.asdrEvents);
  const wsConnected     = useTelemetryStore((s) => s.wsConnected);
  const ownShip         = useTelemetryStore((s) => s.ownShip);
  const targets         = useTelemetryStore((s) => s.targets);
  const modulePulses    = useTelemetryStore((s) => s.modulePulses);
  const sat2            = useTelemetryStore((s) => s.sat2);
  const sat3            = useTelemetryStore((s) => s.sat3);
  const sotifMetrics    = useTelemetryStore((s) => s.sotifMetrics);

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
    g: () => setViewMode(viewMode === 'engineer' ? 'captain' : 'engineer'),
    v: () => setViewMode(viewMode === 'roc' ? 'captain' : 'roc'),
    p: () => handlePause(),
    r: () => handlePlay(),
    s: handleStop,
  } as any);

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

  useEffect(() => {
    if (wsConnected) {
      setUseDemo(false);
      return;
    }
    if (lcState !== 3) {
      setUseDemo(false);
      return;
    }
    const timer = setTimeout(() => {
      setUseDemo(true);
    }, 3000);
    return () => clearTimeout(timer);
  }, [lcState, wsConnected]);

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

        <div style={{ position: 'absolute', bottom: 64, left: '50%', transform: 'translateX(-50%)', zIndex: 15 }}>
          <DistanceScale nmPerPixel={0.01} />
        </div>

        
        {/* Engineer info overlay panels — E2E test targets (sat2, sat3, sotif) */}
        {(isEngineer || viewMode === 'god') && (
          <div style={{
            position: 'absolute', top: 24, right: 16, zIndex: 25,
            display: 'flex', gap: 6, flexDirection: 'column',
            fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-1)',
            pointerEvents: 'none',
          }}>
            {sat2 && (
              <div data-testid="ivp-contribution-panel" style={{
                background: 'rgba(7,12,19,0.88)', border: '1px solid var(--line-2)',
                borderRadius: 4, padding: '5px 10px', minWidth: 150,
              }}>
                <span style={{ color: 'var(--c-phos)', fontSize: 8, letterSpacing: '0.1em', textTransform: 'uppercase' }}>
                  M4 IvP
                </span>
                <div style={{ marginTop: 1 }}>
                  {sat2.active_behavior ?? '-'} @ {(sat2.active_behavior_weight * 100).toFixed(0)}%
                  {' | '}{sat2.reasoning_latency_ms}ms
                </div>
              </div>
            )}
            {sat3 && (
              <div data-testid="trajectory-panel" style={{
                background: 'rgba(7,12,19,0.88)', border: '1px solid var(--line-2)',
                borderRadius: 4, padding: '5px 10px', minWidth: 150,
              }}>
                <span style={{ color: 'var(--c-phos)', fontSize: 8, letterSpacing: '0.1em', textTransform: 'uppercase' }}>
                  M5 Trajectory
                </span>
                <div style={{ marginTop: 1 }}>
                  {sat3.trajectory_candidates.length} candidates
                  {sat3.uncertainty_bands ? ' · bands' : ''}
                </div>
              </div>
            )}
            {sotifMetrics && (
              <div data-testid="sotif-metrics-panel" style={{
                background: 'rgba(7,12,19,0.88)', border: '1px solid var(--line-2)',
                borderRadius: 4, padding: '5px 10px', minWidth: 150,
              }}>
                <span style={{ color: 'var(--c-phos)', fontSize: 8, letterSpacing: '0.1em', textTransform: 'uppercase' }}>
                  M7 SOTIF
                </span>
                <div style={{ marginTop: 1 }}>
                  σ={sotifMetrics.ais_radar_consistency_sigma.toFixed(1)}
                  {' | '}RMS={sotifMetrics.target_predictability_rms_m.toFixed(0)}m
                  {' | '}cov={sotifMetrics.perception_coverage_pct.toFixed(0)}%
                </div>
              </div>
            )}
          </div>
        )}

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
            <button
              data-testid="right-drawer-toggle"
              onClick={toggleRight}
              style={{
                position: 'absolute', top: '50%', right: rightDrawerOpen ? 284 : 4,
                transform: 'translateY(-50%)', zIndex: 25,
                background: 'rgba(7,12,19,0.8)', border: '1px solid var(--line-2)',
                color: rightDrawerOpen ? 'var(--c-phos)' : 'var(--txt-3)',
                padding: '6px 4px', cursor: 'pointer', borderRadius: '4px 0 0 4px',
                transition: 'right 0.2s',
              }}
            ><LucidePanelRight size={14} /></button>
          </>
        )}

        {(isEngineer || viewMode === 'god') && leftDrawerOpen  && <LeftDrawer />}
        {(isEngineer || viewMode === 'god') && rightDrawerOpen && <RightDrawer />}

        {viewMode === 'god' && <FaultInjectPanel />}

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
        {useDemo && (
          <div style={{
            display: 'flex', alignItems: 'center', gap: 6,
            borderLeft: '1px solid var(--line-2)', paddingLeft: 24,
            marginLeft: 8,
          }}>
            <div style={{
              background: 'rgba(248,81,73,0.15)',
              border: '1px solid var(--c-danger)',
              borderRadius: 'var(--radius-none)',
              padding: '3px 10px',
              fontFamily: 'var(--f-mono)',
              fontSize: 9,
              color: 'var(--c-danger)',
              letterSpacing: '0.08em',
              fontWeight: 600,
            }}>
              DEMO MODE — dead-reckon fallback
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
