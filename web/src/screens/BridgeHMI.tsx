import { useRef, useEffect, useState } from 'react';
import { SilMapView } from '../map/SilMapView';
import { useFoxgloveLive } from '../hooks/useFoxgloveLive';
import { useTelemetryStore, useControlStore, useUIStore } from '../store';
import { useDeactivateLifecycleMutation, useTriggerFaultMutation } from '../api/silApi';
import type { ASDREvent } from '../store/telemetryStore';
import { CompassRose } from '../map/CompassRose';
import { PpiRings } from '../map/PpiRings';
import { ColregsSectors } from '../map/ColregsSectors';
import { ImazuGeometry } from '../map/ImazuGeometry';
import { DistanceScale } from '../map/DistanceScale';
import { ArpaTargetTable } from './shared/ArpaTargetTable';
import { ModuleDrilldown } from './shared/ModuleDrilldown';
import { ScoringGauges } from './shared/ScoringGauges';
import { TorModal } from './shared/TorModal';
import { FaultInjectPanel } from './shared/FaultInjectPanel';
import { ConningBar } from './shared/ConningBar';
import { ThreatRibbon } from './shared/ThreatRibbon';
import { useFsmStore } from '../store';
import { useHotkeys } from '../hooks/useHotkeys';

// ── constants ─────────────────────────────────────────────────────────────────
const MODULE_NAMES = ['M1', 'M2', 'M3', 'M4', 'M5', 'M6', 'M7', 'M8'];
const HEALTH_COLOR: Record<number, string> = { 1: '#34d399', 2: '#fbbf24', 3: '#f87171' };
const VERDICT_COLOR: Record<number, string> = { 1: '#34d399', 2: '#fbbf24', 3: '#f87171' };
const VERDICT_LABEL: Record<number, string> = { 1: 'PASS', 2: 'RISK', 3: 'FAIL' };

const FAULT_TYPES = ['ais_dropout', 'radar_noise_spike', 'disturbance_step'] as const;

function fmtSimTime(secs: number) {
  const m = Math.floor(secs / 60).toString().padStart(2, '0');
  const s = Math.floor(secs % 60).toString().padStart(2, '0');
  return `${m}:${s}`;
}

// ── sub-components ────────────────────────────────────────────────────────────

/** Top info bar — CPA / TCPA / Rule / Decision (IEC 62288 §SA-4) */
function TopInfoBar({ asdrEvents }: { asdrEvents: ASDREvent[] }) {
  // Derive latest values from ASDR event stream
  const latest = asdrEvents.length ? asdrEvents[asdrEvents.length - 1] : null;
  const cpaEvt = [...asdrEvents].reverse().find((e) => e.event_type === 'cpa_update');
  const ruleEvt = [...asdrEvents].reverse().find((e) => e.rule_ref);

  let cpaTxt = '—';
  let tcpaTxt = '—';
  let ruleTxt = '—';
  let decisionTxt = '—';
  let verdictNum = 0;

  if (cpaEvt?.payload_json) {
    try {
      const p = JSON.parse(cpaEvt.payload_json);
      if (p.cpa_nm !== undefined) cpaTxt = `${Number(p.cpa_nm).toFixed(2)} nm`;
      if (p.tcpa_min !== undefined) tcpaTxt = `${Number(p.tcpa_min).toFixed(1)} min`;
    } catch { /* noop */ }
  }
  if (ruleEvt?.rule_ref) ruleTxt = ruleEvt.rule_ref;
  if (latest?.event_type) decisionTxt = latest.event_type.replace(/_/g, ' ');
  if (latest?.verdict) verdictNum = latest.verdict;

  const vColor = VERDICT_COLOR[verdictNum] ?? '#888';
  const vLabel = VERDICT_LABEL[verdictNum] ?? '—';

  return (
    <div style={{
      position: 'absolute', top: 0, left: 0, right: 0, zIndex: 20,
      height: 44,
      background: 'linear-gradient(180deg,rgba(11,19,32,0.97) 0%,rgba(11,19,32,0.80) 100%)',
      borderBottom: '1px solid rgba(45,212,191,0.18)',
      display: 'flex', alignItems: 'center', padding: '0 16px', gap: 28,
      fontFamily: "'JetBrains Mono', 'Courier New', monospace", fontSize: 12,
    }}>
      <InfoChip label="CPA" value={cpaTxt} accent="#f87171" />
      <InfoChip label="TCPA" value={tcpaTxt} accent="#fbbf24" />
      <InfoChip label="RULE" value={ruleTxt} accent="#60a5fa" />
      <InfoChip label="DECISION" value={decisionTxt} accent="#c084fc" />
      {verdictNum > 0 && (
        <div style={{
          marginLeft: 'auto', padding: '2px 10px', borderRadius: 4,
          background: vColor + '22', border: `1px solid ${vColor}`,
          color: vColor, fontWeight: 700, letterSpacing: 1,
        }}>
          {vLabel}
        </div>
      )}
    </div>
  );
}

function InfoChip({ label, value, accent }: { label: string; value: string; accent: string }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', lineHeight: 1 }}>
      <span style={{ color: '#4b6888', fontSize: 9, letterSpacing: 1.5, textTransform: 'uppercase' }}>{label}</span>
      <span style={{ color: accent, fontWeight: 600, fontSize: 13, marginTop: 1 }}>{value}</span>
    </div>
  );
}

/** Captain HUD — own-ship nav data + wind/sea (left overlay) */
function CaptainHUD() {
  const ownShip = useTelemetryStore((s) => s.ownShip);
  const env     = useTelemetryStore((s) => s.environment);

  const sogKn   = ownShip ? ((ownShip.kinematics?.sog ?? 0) * 1.944).toFixed(1) : '—';
  const cogDeg  = ownShip ? (((ownShip.kinematics?.cog  ?? 0) * 180 / Math.PI + 360) % 360).toFixed(1) : '—';
  const hdgDeg  = ownShip ? (((ownShip.pose?.heading    ?? 0) * 180 / Math.PI + 360) % 360).toFixed(1) : '—';
  const rotDpm  = ownShip ? ((ownShip.kinematics?.rot   ?? 0) * 180 / Math.PI * 60).toFixed(1) : '—';

  const windDirDeg = env ? ((env.wind?.direction ?? 0) * 180 / Math.PI).toFixed(0) : '—';
  const windKn     = env ? ((env.wind?.speedMps  ?? 0) * 1.944).toFixed(1) : '—';
  const visNm      = env ? (env.visibilityNm ?? 0).toFixed(1) : '—';
  const seaB       = env?.seaStateBeaufort ?? '—';

  return (
    <div style={{
      position: 'absolute', top: 54, left: 16, zIndex: 15,
      background: 'rgba(11,19,32,0.88)',
      border: '1px solid rgba(45,212,191,0.22)',
      borderRadius: 8, padding: '10px 14px',
      fontFamily: "'JetBrains Mono', monospace", fontSize: 12, color: '#e6edf3',
      minWidth: 175, backdropFilter: 'blur(6px)',
    }}>
      {ownShip ? (
        <>
          <HudRow label="SOG" value={`${sogKn} kn`}    accent="#2dd4bf" />
          <HudRow label="COG" value={`${cogDeg}°`}     accent="#2dd4bf" />
          <HudRow label="HDG" value={`${hdgDeg}°`}     accent="#60a5fa" />
          <HudRow label="ROT" value={`${rotDpm}°/min`} accent="#a3e635" />
        </>
      ) : (
        <div style={{ color: '#4b6888', fontSize: 11 }}>Waiting for telemetry…</div>
      )}
      {env && (
        <>
          <div style={{ height: 1, background: 'rgba(45,212,191,0.15)', margin: '8px 0' }} />
          <HudRow label="WIND" value={`${windDirDeg}° ${windKn}kn`} accent="#93c5fd" />
          <HudRow label="SEA"  value={`Bft ${seaB}`}                 accent="#93c5fd" />
          <HudRow label="VIS"  value={`${visNm} nm`}                 accent="#93c5fd" />
        </>
      )}
    </div>
  );
}

function HudRow({ label, value, accent }: { label: string; value: string; accent: string }) {
  return (
    <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 3 }}>
      <span style={{ color: '#4b6888', fontSize: 10, letterSpacing: 1, marginRight: 12 }}>{label}</span>
      <span style={{ color: accent, fontWeight: 600 }}>{value}</span>
    </div>
  );
}

/** Module Pulse bar — M1-M8 with labels, latency, tooltip */
function ModulePulseBar() {
  const pulses = useTelemetryStore((s) => s.modulePulses);

  // Build lookup by module_id (1-based)
  const byId: Record<number, (typeof pulses)[0]> = {};
  for (const p of pulses) { if (p.moduleId != null) byId[Number(p.moduleId)] = p; }

  return (
    <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
      {MODULE_NAMES.map((name, i) => {
        const p = byId[i + 1];
        const color = p ? (HEALTH_COLOR[p.state ?? 0] ?? '#444') : '#333';
        const tip = p
          ? `${name}: ${p.state === 1 ? 'GREEN' : p.state === 2 ? 'AMBER' : 'RED'} | ${p.latencyMs ?? '?'}ms`
          : `${name}: no data`;
        return (
          <div key={name} title={tip}
               style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2 }}>
            <div style={{
              width: 14, height: 14, borderRadius: '50%',
              background: color,
              boxShadow: p && p.state === 1 ? `0 0 6px ${color}88` : 'none',
              transition: 'background 0.4s, box-shadow 0.4s',
            }} />
            <span style={{ color: '#4b6888', fontSize: 8, letterSpacing: 0.5 }}>{name}</span>
          </div>
        );
      })}
    </div>
  );
}

/** ASDR collapsible log panel */
function AsdrLogPanel({ events, expanded }: { events: ASDREvent[]; expanded: boolean }) {
  const endRef = useRef<HTMLDivElement>(null);
  useEffect(() => { if (expanded) endRef.current?.scrollIntoView({ behavior: 'smooth' }); }, [events, expanded]);
  if (!expanded) return null;
  return (
    <div style={{
      position: 'absolute', bottom: 52, left: 16, right: 16, zIndex: 25,
      maxHeight: 220,
      background: 'rgba(11,19,32,0.96)',
      border: '1px solid rgba(45,212,191,0.20)',
      borderRadius: 8, padding: '8px 10px',
      overflowY: 'auto', fontFamily: 'monospace', fontSize: 10, color: '#9ca3af',
    }}>
      <div style={{ color: '#2dd4bf', fontWeight: 700, marginBottom: 4, fontSize: 11 }}>
        ASDR EVENT LOG ({events.length})
      </div>
      {events.length === 0 ? (
        <div style={{ color: '#333' }}>No events yet</div>
      ) : (
        [...events].reverse().map((e, i) => {
          const vColor = VERDICT_COLOR[e.verdict ?? 0] ?? '#9ca3af';
          return (
            <div key={i} style={{ marginBottom: 2, borderBottom: '1px solid rgba(255,255,255,0.04)', paddingBottom: 2 }}>
              <span style={{ color: '#4b6888' }}>{String(i).padStart(3, '0')} </span>
              <span style={{ color: '#60a5fa' }}>{e.event_type} </span>
              {e.rule_ref && <span style={{ color: '#a3e635' }}>[{e.rule_ref}] </span>}
              {e.verdict != null && <span style={{ color: vColor }}>{VERDICT_LABEL[e.verdict] ?? ''} </span>}
            </div>
          );
        })
      )}
      <div ref={endRef} />
    </div>
  );
}

/** Fault injection modal */
function FaultInjectModal({ onClose }: { onClose: () => void }) {
  const [selectedFault, setSelectedFault] = useState<string>(FAULT_TYPES[0]);
  const [triggerFault, { isLoading }] = useTriggerFaultMutation();
  const [result, setResult] = useState<string | null>(null);

  const handleInject = async () => {
    try {
      const r = await triggerFault({ fault_type: selectedFault, payload_json: '{}' }).unwrap();
      setResult(`Injected: ${r.fault_id ?? 'ok'}`);
    } catch (e: any) {
      setResult(`Error: ${e?.data?.detail ?? 'unknown'}`);
    }
  };

  return (
    <div style={{
      position: 'absolute', top: '50%', left: '50%', transform: 'translate(-50%,-50%)',
      zIndex: 50, background: '#0f1929', border: '1px solid #fbbf24',
      borderRadius: 10, padding: 20, minWidth: 320, color: '#e6edf3',
      boxShadow: '0 8px 40px rgba(0,0,0,0.6)',
    }}>
      <div style={{ fontSize: 14, fontWeight: 700, color: '#fbbf24', marginBottom: 12 }}>
        ⚠ Inject Fault
      </div>
      <div style={{ marginBottom: 10 }}>
        {FAULT_TYPES.map((ft) => (
          <label key={ft} style={{ display: 'block', marginBottom: 6, cursor: 'pointer', fontSize: 12 }}>
            <input type="radio" name="fault" value={ft} checked={selectedFault === ft}
                   onChange={() => setSelectedFault(ft)}
                   style={{ marginRight: 8 }} />
            {ft.replace(/_/g, ' ')}
          </label>
        ))}
      </div>
      <div style={{ display: 'flex', gap: 8 }}>
        <button onClick={handleInject} disabled={isLoading} style={{
          background: '#dc2626', color: '#fff', border: 'none', borderRadius: 4,
          padding: '6px 18px', cursor: 'pointer', fontWeight: 700, flex: 1,
        }}>
          {isLoading ? 'Injecting…' : 'Inject'}
        </button>
        <button onClick={onClose} style={{
          background: '#1f2937', color: '#9ca3af', border: '1px solid #374151',
          borderRadius: 4, padding: '6px 14px', cursor: 'pointer',
        }}>Cancel</button>
      </div>
      {result && <div style={{ marginTop: 8, fontSize: 11, color: '#a3e635' }}>{result}</div>}
    </div>
  );
}

// ── Main BridgeHMI screen ─────────────────────────────────────────────────────
export function BridgeHMI() {
  useFoxgloveLive();

  const lifecycleStatus = useTelemetryStore((s) => s.lifecycleStatus);
  const asdrEvents      = useTelemetryStore((s) => s.asdrEvents);
  const wsConnected     = useTelemetryStore((s) => s.wsConnected);
  const modulePulses    = useTelemetryStore((s) => s.modulePulses);
  const ownShip         = useTelemetryStore((s) => s.ownShip);

  const simRate  = useControlStore((s) => s.simRate);
  const isPaused = useControlStore((s) => s.isPaused);
  const setSimRate = useControlStore((s) => s.setSimRate);
  const setPaused  = useControlStore((s) => s.setPaused);

  const viewMode       = useUIStore((s) => s.viewMode);
  const setViewMode    = useUIStore((s) => s.setViewMode);
  const asdrExpanded   = useUIStore((s) => s.asdrLogExpanded);
  const toggleAsdr     = useUIStore((s) => s.toggleAsdrLog);

  const [showFaultModal, setShowFaultModal] = useState(false);
  const [arpaExpanded, setArpaExpanded] = useState(viewMode === 'god');
  const [drilldownVisible, setDrilldownVisible] = useState(false);
  const [faultPanelOpen, setFaultPanelOpen] = useState(false);
  const [deactivate] = useDeactivateLifecycleMutation();

  const handleStop = async () => {
    await deactivate();
    window.location.hash = '#/report/latest';
  };

  const simTimeSec = lifecycleStatus?.sim_time ?? 0;
  const lcState    = lifecycleStatus?.current_state;
  const lcLabel    = ['?', 'UNCONFIGURED', 'INACTIVE', '▶ ACTIVE', 'DEACTIVATING', 'FINALIZED'][lcState ?? 0] ?? '?';

  // Count pulses by health
  const activePulses = modulePulses.filter((p) => p.state === 1).length;
  const warnPulses   = modulePulses.filter((p) => p.state === 2).length;
  const errPulses    = modulePulses.filter((p) => p.state === 3).length;

  // FSM + hotkeys for runtime semantics (v1.1)
  const fsmCurrent = useFsmStore((s) => s.currentState);
  const fsmSet = useFsmStore((s) => s.setState);

  useHotkeys({
    onTor: () => {
      if (fsmCurrent === 'TOR') return;
      fsmSet('TOR', 'HOTKEY_T', simTimeSec);
    },
    onFault: () => setFaultPanelOpen((v) => !v),
    onMrc: () => fsmSet('MRC', 'HOTKEY_M', simTimeSec),
    onHandback: () => {
      if (fsmCurrent === 'OVERRIDE') fsmSet('TRANSIT', 'HOTKEY_H', simTimeSec);
    },
    onSpace: () => setPaused(!isPaused),
  });

  return (
    <div style={{ height: '100vh', display: 'flex', flexDirection: 'column', background: '#0b1320' }}
         data-testid="bridge-hmi">

      {/* ── Full-screen map area ── */}
      <div style={{ flex: 1, position: 'relative', overflow: 'hidden' }}>
        <SilMapView followOwnShip={viewMode === 'captain'} viewMode={viewMode as 'captain' | 'god'} />

        {/* IEC 62288 §SA-4 top info bar */}
        <TopInfoBar asdrEvents={asdrEvents} />

        {/* Compass Rose — bottom-left, relative bearing in Captain, North-up in God */}
        <div style={{ position: 'absolute', bottom: 56, left: 16, zIndex: 15 }}>
          <CompassRose
            bearing={ownShip ? (ownShip.pose?.heading ?? 0) * 180 / Math.PI : 0}
            relativeMode={viewMode === 'captain'}
          />
        </div>

        {/* PPI Range Rings */}
        <PpiRings
          centerFraction={viewMode === 'captain' ? [50, 70] : [50, 50]}
          radiiPx={[40, 80, 160, 320]}
        />

        {/* COLREGs Sectors — God view only */}
        {viewMode === 'god' && (
          <ColregsSectors
            ownShipFraction={[50, 50]}
            headingDeg={ownShip ? (ownShip.pose?.heading ?? 0) * 180 / Math.PI : 0}
            outerRadiusPx={320}
          />
        )}

        {/* Distance Scale — bottom-center */}
        <div style={{ position: 'absolute', bottom: 56, left: '50%', transform: 'translateX(-50%)', zIndex: 15 }}>
          <DistanceScale nmPerPixel={0.005} />
        </div>

        {/* ARPA Target Table */}
        <ArpaTargetTable expanded={arpaExpanded} onToggle={() => setArpaExpanded(!arpaExpanded)} />

        {/* Scoring Gauges — God view only */}
        <ScoringGauges visible={viewMode === 'god'} />

        {/* Module Drilldown */}
        <ModuleDrilldown visible={drilldownVisible} onClose={() => setDrilldownVisible(false)} />

        {/* Threat Ribbon */}
        <ThreatRibbon viewMode={viewMode as 'captain' | 'god'} />

        {/* Conning Bar */}
        <ConningBar viewMode={viewMode as 'captain' | 'god'} />

        {/* Fault Inject Panel — God view only */}
        {viewMode === 'god' && faultPanelOpen && <FaultInjectPanel />}

        {/* Captain HUD overlay */}
        <CaptainHUD />

        {/* View mode tabs (top right) */}
        <div style={{
          position: 'absolute', top: 54, right: 16, zIndex: 15,
          display: 'flex', gap: 4, flexDirection: 'column',
        }}>
          {(['captain', 'god'] as const).map((mode) => (
            <button key={mode} onClick={() => setViewMode(mode)}
                    id={`view-tab-${mode}`}
                    style={{
                      background: viewMode === mode ? '#2dd4bf22' : 'rgba(11,19,32,0.80)',
                      color: viewMode === mode ? '#2dd4bf' : '#4b6888',
                      border: `1px solid ${viewMode === mode ? '#2dd4bf' : '#1f2d3d'}`,
                      padding: '4px 14px', borderRadius: 4, cursor: 'pointer',
                      fontFamily: 'monospace', fontSize: 11, letterSpacing: 1,
                    }}>
              {mode === 'captain' ? 'CAPTAIN' : 'GOD'}
            </button>
          ))}
        </div>

        {/* WS connection badge */}
        <div style={{
          position: 'absolute', bottom: 16, right: 16, zIndex: 15,
          padding: '3px 10px', borderRadius: 12,
          background: wsConnected ? 'rgba(52,211,153,0.15)' : 'rgba(248,113,113,0.15)',
          border: `1px solid ${wsConnected ? '#34d399' : '#f87171'}`,
          color: wsConnected ? '#34d399' : '#f87171',
          fontFamily: 'monospace', fontSize: 10,
        }}>
          {wsConnected ? '● LIVE' : '○ DISCONNECTED'}
        </div>

        {/* ASDR collapsible log */}
        <AsdrLogPanel events={asdrEvents} expanded={asdrExpanded} />

        {/* Fault injection modal */}
        {showFaultModal && <FaultInjectModal onClose={() => setShowFaultModal(false)} />}
      </div>

      {/* ── Bottom status bar ── */}
      <div style={{
        height: 48, background: '#060e1a',
        borderTop: '1px solid rgba(45,212,191,0.12)',
        display: 'flex', alignItems: 'center', padding: '0 16px', gap: 16,
        fontFamily: "'JetBrains Mono', monospace", fontSize: 11, color: '#e6edf3',
      }}>

        {/* Sim time + lifecycle state */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, minWidth: 180 }}>
          <span style={{ color: '#2dd4bf', fontWeight: 700, fontSize: 14 }}
                data-testid="sim-time">
            {fmtSimTime(simTimeSec)}
          </span>
          <span style={{
            color: lcState === 3 ? '#34d399' : '#fbbf24',
            fontSize: 9, letterSpacing: 1, border: '1px solid currentColor',
            padding: '1px 5px', borderRadius: 3,
          }}>
            {lcLabel}
          </span>
          {lifecycleStatus?.scenario_hash && (
            <span style={{ color: '#1f3a50', fontSize: 9 }} title={lifecycleStatus.scenario_hash}>
              #{lifecycleStatus.scenario_hash.slice(0, 8)}
            </span>
          )}
        </div>

        {/* Speed controls */}
        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <button onClick={() => setPaused(!isPaused)}
                  id="btn-pause-resume"
                  style={{
                    background: isPaused ? '#fbbf2433' : '#1f2937',
                    color: isPaused ? '#fbbf24' : '#9ca3af',
                    border: `1px solid ${isPaused ? '#fbbf24' : '#374151'}`,
                    padding: '3px 12px', borderRadius: 4, cursor: 'pointer', fontSize: 13,
                  }}>
            {isPaused ? '▶' : '⏸'}
          </button>
          {[0.5, 1, 2, 4, 10].map((r) => (
            <button key={r} onClick={() => setSimRate(r)}
                    style={{
                      background: simRate === r ? '#2dd4bf22' : '#1f2937',
                      color: simRate === r ? '#2dd4bf' : '#4b6888',
                      border: `1px solid ${simRate === r ? '#2dd4bf' : '#374151'}`,
                      padding: '3px 10px', borderRadius: 4, cursor: 'pointer', fontSize: 11,
                    }}>
              ×{r}
            </button>
          ))}
        </div>

        {/* Module Pulse */}
        <div style={{ flex: 1, display: 'flex', justifyContent: 'center', cursor: 'pointer' }} onClick={() => setDrilldownVisible(!drilldownVisible)}>
          <ModulePulseBar />
        </div>

        {/* Health summary badges */}
        {modulePulses.length > 0 && (
          <div style={{ display: 'flex', gap: 6, fontSize: 10 }}>
            <span style={{ color: '#34d399' }}>●{activePulses}</span>
            <span style={{ color: '#fbbf24' }}>●{warnPulses}</span>
            <span style={{ color: '#f87171' }}>●{errPulses}</span>
          </div>
        )}

        {/* Right actions */}
        <div style={{ display: 'flex', gap: 6, marginLeft: 'auto' }}>
          <button onClick={toggleAsdr}
                  id="btn-asdr-toggle"
                  style={{
                    background: asdrExpanded ? '#2dd4bf22' : '#1f2937',
                    color: asdrExpanded ? '#2dd4bf' : '#4b6888',
                    border: `1px solid ${asdrExpanded ? '#2dd4bf' : '#374151'}`,
                    padding: '4px 12px', borderRadius: 4, cursor: 'pointer', fontSize: 11,
                  }}>
            ASDR {asdrEvents.length > 0 ? `(${asdrEvents.length})` : ''}
          </button>

          <button onClick={() => setShowFaultModal(true)}
                  id="btn-inject-fault"
                  style={{
                    background: '#7c1d1d', color: '#fca5a5',
                    border: '1px solid #dc2626',
                    padding: '4px 12px', borderRadius: 4, cursor: 'pointer', fontSize: 11,
                  }}>
            ⚠ FAULT
          </button>

          <button onClick={handleStop}
                  id="btn-stop"
                  style={{
                    background: '#7f1d1d', color: '#fff',
                    border: '1px solid #ef4444',
                    padding: '4px 16px', borderRadius: 4, cursor: 'pointer', fontWeight: 700,
                  }}>
            ⏹ STOP
          </button>
        </div>
      </div>

      {/* TorModal — renders via portal, always mounted */}
      <TorModal />
    </div>
  );
}
