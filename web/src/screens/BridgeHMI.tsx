import { useRef, useEffect, useState } from 'react';
import { SilMapView } from '../map/SilMapView';
import { useFoxgloveLive } from '../hooks/useFoxgloveLive';
import { useTelemetryStore, useControlStore, useUIStore } from '../store';
import { useDeactivateLifecycleMutation, useTriggerFaultMutation } from '../api/silApi';
import type { ASDREvent } from '../store/telemetryStore';
import { CompassRose } from '../map/CompassRose';
import { PpiRings } from '../map/PpiRings';
import { ColregsSectors } from '../map/ColregsSectors';
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
import { LucidePlay, LucidePause, LucideSquare, LucideRotateCcw, LucideSettings2, LucideTerminalSquare, LucideAlertTriangle } from 'lucide-react';

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

/** Captain HUD — own-ship nav data + wind/sea (left overlay) */
function CaptainHUD({ asdrEvents }: { asdrEvents: ASDREvent[] }) {
  const ownShip = useTelemetryStore((s) => s.ownShip);
  const env     = useTelemetryStore((s) => s.environment);

  const sogKn   = ownShip ? ((ownShip.kinematics?.sog ?? 0) * 1.944).toFixed(1) : '—';
  const cogDeg  = ownShip ? (((ownShip.kinematics?.cog  ?? 0) * 180 / Math.PI + 360) % 360).toFixed(1) : '—';
  const hdgDeg  = ownShip ? (((ownShip.pose?.heading    ?? 0) * 180 / Math.PI + 360) % 360).toFixed(1) : '—';
  const rotDpm  = ownShip ? ((ownShip.kinematics?.rot   ?? 0) * 180 / Math.PI * 60).toFixed(1) : '—';

  const windDirDeg = env ? ((env.wind?.direction ?? 0) * 180 / Math.PI).toFixed(0) : '—';
  const windKn     = env ? ((env.wind?.speedMps  ?? 0) * 1.944).toFixed(1) : '—';
  
  const latest = asdrEvents.length ? asdrEvents[asdrEvents.length - 1] : null;
  const cpaEvt = [...asdrEvents].reverse().find((e) => e.event_type === 'cpa_update');
  const ruleEvt = [...asdrEvents].reverse().find((e) => e.rule_ref);
  let cpaTxt = '—';
  let tcpaTxt = '—';
  let ruleTxt = '—';
  let decisionTxt = '—';

  if (cpaEvt?.payload_json) {
    try {
      const p = JSON.parse(cpaEvt.payload_json);
      if (p.cpa_nm !== undefined) cpaTxt = `${Number(p.cpa_nm).toFixed(2)} nm`;
      if (p.tcpa_min !== undefined) tcpaTxt = `${Number(p.tcpa_min).toFixed(1)} min`;
    } catch { /* noop */ }
  }
  if (ruleEvt?.rule_ref) ruleTxt = ruleEvt.rule_ref;
  if (latest?.event_type) decisionTxt = latest.event_type.replace(/_/g, ' ');

  return (
    <div className="glass-panel" style={{
      position: 'absolute', top: 40, left: 16, zIndex: 15,
      borderRadius: 8, padding: '16px 20px',
      fontFamily: "'JetBrains Mono', monospace", fontSize: 13, color: '#e6edf3',
      minWidth: 220
    }}>
      <HudRow label="CPA" value={cpaTxt}    accent="#f87171" />
      <HudRow label="TCPA" value={tcpaTxt}   accent="#fbbf24" />
      <div style={{ height: 1, background: 'rgba(255,255,255,0.1)', margin: '8px 0' }} />
      <HudRow label="Rule" value={ruleTxt}   accent="#60a5fa" />
      <HudRow label="Decis" value={decisionTxt} accent="#c084fc" />
      <div style={{ height: 1, background: 'rgba(255,255,255,0.1)', margin: '8px 0' }} />
      <HudRow label="SOG" value={`${sogKn} kn`}    accent="#2dd4bf" />
      <HudRow label="COG" value={`${cogDeg}°`}     accent="#2dd4bf" />
      <HudRow label="HDG" value={`${hdgDeg}°`}     accent="#60a5fa" />
      <HudRow label="ROT" value={`${rotDpm}°/m`} accent="#a3e635" />
    </div>
  );
}

function HudRow({ label, value, accent }: { label: string; value: string; accent: string }) {
  return (
    <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 4 }}>
      <span style={{ color: '#8A9AAD', fontSize: 11, letterSpacing: 1 }}>{label}</span>
      <span style={{ color: accent, fontWeight: 700 }}>{value}</span>
    </div>
  );
}

/** Module Pulse bar — M1-M8 with labels, latency, tooltip (Top fixed) */
function ModulePulseBar() {
  const pulses = useTelemetryStore((s) => s.modulePulses);
  const byId: Record<number, (typeof pulses)[0]> = {};
  for (const p of pulses) { if (p.moduleId != null) byId[Number(p.moduleId)] = p; }

  return (
    <div style={{ 
      display: 'flex', gap: 2, alignItems: 'center', height: 16, background: '#070C13',
      position: 'absolute', top: 0, left: 0, right: 0, zIndex: 30, justifyContent: 'center'
    }}>
      {MODULE_NAMES.map((name, i) => {
        const p = byId[i + 1];
        const color = p ? (HEALTH_COLOR[p.state ?? 0] ?? '#444') : '#333';
        const tip = p ? `${name}: ${p.state === 1 ? 'GREEN' : p.state === 2 ? 'AMBER' : 'RED'} | ${p.latencyMs ?? '?'}ms` : `${name}: no data`;
        return (
          <div key={name} title={tip} style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '0 8px', borderRight: '1px solid #1B2C44' }}>
            <div style={{ width: 6, height: 6, borderRadius: '50%', background: color }} />
            <span style={{ color: '#8A9AAD', fontSize: 9, fontFamily: 'var(--f-mono)' }}>{name}</span>
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
    <div className="glass-panel" style={{
      position: 'absolute', bottom: 56, right: 16, zIndex: 25,
      maxHeight: 220, width: 340,
      borderRadius: 8, padding: '12px 14px',
      overflowY: 'auto', fontFamily: 'var(--f-mono)', fontSize: 11, color: '#C5D2E0',
    }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 6, color: '#2dd4bf', fontWeight: 700, marginBottom: 8, fontSize: 12 }}>
        <LucideTerminalSquare size={14} /> ASDR LOG ({events.length})
      </div>
      {events.length === 0 ? (
        <div style={{ color: '#566578' }}>No events yet</div>
      ) : (
        [...events].reverse().map((e, i) => {
          const vColor = VERDICT_COLOR[e.verdict ?? 0] ?? '#9ca3af';
          return (
            <div key={i} style={{ marginBottom: 4, borderBottom: '1px solid rgba(255,255,255,0.04)', paddingBottom: 4 }}>
              <span style={{ color: '#566578' }}>{String(i).padStart(3, '0')} </span>
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

// ── Main BridgeHMI screen ─────────────────────────────────────────────────────
export function BridgeHMI() {
  useFoxgloveLive();

  const lifecycleStatus = useTelemetryStore((s) => s.lifecycleStatus);
  const asdrEvents      = useTelemetryStore((s) => s.asdrEvents);
  const wsConnected     = useTelemetryStore((s) => s.wsConnected);
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
  const [deactivate] = useDeactivateLifecycleMutation();
  const autoNavRef = useRef(false);

  const handleStop = async () => {
    await deactivate();
    window.location.hash = '#/report/latest';
  };

  const simTimeSec = lifecycleStatus?.sim_time ?? 0;
  const lcState    = lifecycleStatus?.current_state;
  const lcLabel    = ['?', 'UNCONFIGURED', 'INACTIVE', '▶ ACTIVE', 'DEACTIVATING', 'FINALIZED'][lcState ?? 0] ?? '?';

  // Auto-navigate to Report when simulation completes
  useEffect(() => {
    if (lcState === 5 && !autoNavRef.current) {
      autoNavRef.current = true;
      // Small delay so user sees the final state before transition
      const timer = setTimeout(() => {
        window.location.hash = '#/report/latest';
      }, 1500);
      return () => clearTimeout(timer);
    }
  }, [lcState]);

  return (
    <div style={{ height: '100%', display: 'flex', flexDirection: 'column', background: 'var(--bg-0)' }}
         data-testid="bridge-hmi">

      {/* ── Full-screen map area ── */}
      <div style={{ flex: 1, position: 'relative', overflow: 'hidden' }}>
        <ModulePulseBar />
        <SilMapView followOwnShip={viewMode === 'captain'} viewMode={viewMode as 'captain' | 'god'} />

        {/* Waiting overlay — shown until first WS frame arrives */}
        {!ownShip && (
          <div style={{
            position: 'absolute', inset: 0, zIndex: 50,
            display: 'flex', flexDirection: 'column',
            alignItems: 'center', justifyContent: 'center',
            background: 'rgba(7,12,19,0.88)',
            fontFamily: 'var(--f-mono)',
          }}>
            {/* Animated radar sweep */}
            <div style={{
              width: 80, height: 80, borderRadius: '50%',
              border: '2px solid rgba(45,212,191,0.2)',
              position: 'relative', marginBottom: 24,
            }}>
              <div style={{
                position: 'absolute', inset: 0, borderRadius: '50%',
                border: '2px solid transparent',
                borderTopColor: 'var(--c-phos)',
                animation: 'spin 2s linear infinite',
              }} />
              <div style={{
                position: 'absolute', top: '50%', left: '50%',
                transform: 'translate(-50%, -50%)',
                width: 8, height: 8, borderRadius: '50%',
                background: 'var(--c-phos)', opacity: 0.8,
                boxShadow: '0 0 16px var(--c-phos)',
                animation: 'pulse-glow 1.5s ease-in-out infinite',
              }} />
            </div>
            <div style={{ fontSize: 14, color: 'var(--txt-1)', marginBottom: 8, letterSpacing: '0.16em', fontWeight: 600 }}>
              AWAITING TELEMETRY
            </div>
            <div style={{ fontSize: 11, color: 'var(--txt-3)', marginBottom: 16 }}>
              Connecting to simulation data feed…
            </div>
            <div style={{
              display: 'flex', alignItems: 'center', gap: 8,
              padding: '6px 16px', borderRadius: 20,
              background: wsConnected ? 'rgba(45,212,191,0.1)' : 'rgba(248,81,73,0.1)',
              border: `1px solid ${wsConnected ? 'rgba(45,212,191,0.3)' : 'rgba(248,81,73,0.3)'}`,
            }}>
              <div style={{
                width: 6, height: 6, borderRadius: '50%',
                background: wsConnected ? '#2dd4bf' : '#f87171',
                animation: wsConnected ? 'pulse-glow 1.5s ease-in-out infinite' : 'none',
              }} />
              <span style={{ fontSize: 10, color: wsConnected ? '#2dd4bf' : '#f87171' }}>
                {wsConnected ? 'WS CONNECTED' : 'WS DISCONNECTED'} · ws://127.0.0.1:8765
              </span>
            </div>
            <style>{`
              @keyframes spin { to { transform: rotate(360deg); } }
              @keyframes pulse-glow {
                0%, 100% { opacity: 0.5; box-shadow: 0 0 8px var(--c-phos); }
                50% { opacity: 1; box-shadow: 0 0 24px var(--c-phos); }
              }
            `}</style>
          </div>
        )}

        <div style={{ position: 'absolute', bottom: 64, left: 16, zIndex: 15 }}>
          <CompassRose bearing={ownShip ? (ownShip.pose?.heading ?? 0) * 180 / Math.PI : 0} relativeMode={viewMode === 'captain'} />
        </div>

        <PpiRings centerFraction={viewMode === 'captain' ? [50, 70] : [50, 50]} radiiPx={[40, 80, 160, 320]} />

        {viewMode === 'god' && <ColregsSectors ownShipFraction={[50, 50]} headingDeg={ownShip ? (ownShip.pose?.heading ?? 0) * 180 / Math.PI : 0} outerRadiusPx={320} />}

        <div style={{ position: 'absolute', bottom: 64, left: '50%', transform: 'translateX(-50%)', zIndex: 15 }}>
          <DistanceScale nmPerPixel={0.01} />
        </div>

        <CaptainHUD asdrEvents={asdrEvents} />

        <div className="glass-panel" style={{
          position: 'absolute', top: 40, right: 16, zIndex: 15,
          display: 'flex', gap: 4, borderRadius: 6, padding: 4
        }}>
          {(['captain', 'god'] as const).map((mode) => (
            <button key={mode} onClick={() => setViewMode(mode)}
                    style={{
                      background: viewMode === mode ? '#2dd4bf22' : 'transparent',
                      color: viewMode === mode ? '#2dd4bf' : '#8A9AAD',
                      border: 'none', padding: '6px 16px', borderRadius: 4, cursor: 'pointer',
                      fontFamily: 'var(--f-disp)', fontSize: 11, letterSpacing: 1, fontWeight: viewMode === mode ? 700 : 500
                    }}>
              {mode === 'captain' ? 'CAPTAIN' : 'GOD'}
            </button>
          ))}
        </div>

        <AsdrLogPanel events={asdrEvents} expanded={asdrExpanded} />
      </div>

      {/* Bottom Control Toolbar */}
      <div style={{
        height: 48, background: 'var(--bg-1)',
        borderTop: '1px solid var(--line-2)',
        display: 'flex', alignItems: 'center', padding: '0 24px', gap: 24,
        fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--txt-1)',
        flexShrink: 0,
      }}>
        {/* Playback Controls */}
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <button onClick={() => setPaused(false)} style={{ background: 'transparent', color: !isPaused ? 'var(--c-phos)' : 'var(--txt-2)', border: 'none', cursor: 'pointer' }}>
            <LucidePlay size={20} />
          </button>
          <button onClick={() => setPaused(true)} style={{ background: 'transparent', color: isPaused ? 'var(--c-warn)' : 'var(--txt-2)', border: 'none', cursor: 'pointer' }}>
            <LucidePause size={20} />
          </button>
          <button onClick={handleStop} style={{ background: 'transparent', color: 'var(--c-danger)', border: 'none', cursor: 'pointer', marginLeft: 8 }}>
            <LucideSquare size={20} />
          </button>
          <button style={{ background: 'transparent', color: 'var(--txt-2)', border: 'none', cursor: 'pointer' }}>
            <LucideRotateCcw size={20} />
          </button>
        </div>

        {/* Time Scrubber (Placeholder) */}
        <div style={{ flex: 1, display: 'flex', alignItems: 'center', gap: 12 }}>
           <input type="range" min="0" max="600" value={simTimeSec} style={{ flex: 1, accentColor: 'var(--c-phos)' }} readOnly />
           <span style={{ color: 'var(--txt-1)' }}>{fmtSimTime(simTimeSec)} / 10:00</span>
        </div>

        {/* Speed */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
           <select value={simRate} onChange={(e) => setSimRate(Number(e.target.value))} style={{ background: 'var(--bg-2)', color: 'var(--c-phos)', border: '1px solid var(--line-2)', padding: '4px 8px', borderRadius: 4, fontFamily: 'var(--f-mono)' }}>
             <option value={0.5}>0.5x</option>
             <option value={1}>1.0x</option>
             <option value={2}>2.0x</option>
             <option value={4}>4.0x</option>
             <option value={10}>10.0x</option>
           </select>
        </div>

        {/* Tools */}
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
