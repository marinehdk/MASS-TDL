import { useEffect, useState } from 'react';
import {
  useExportMarzipMutation,
  useGetExportStatusQuery,
  useGetLastRunScoringQuery,
} from '../api/silApi';
import { useScenarioStore } from '../store';
import { TimelineSixLane } from './shared/TimelineSixLane';
import { AsdrLedger } from './shared/AsdrLedger';
import { TrajectoryReplay } from './shared/TrajectoryReplay';
import { ScoringRadarChart } from './shared/ScoringRadarChart';

interface KpiCardProps {
  label: string;
  value: string;
  unit: string;
}

function KpiCard({ label, value, unit }: KpiCardProps) {
  return (
    <div style={{
      border: '1px solid #444', borderRadius: 8, padding: 16,
      minWidth: 160, background: '#1a1a2e',
    }}>
      <div style={{ fontSize: 12, color: '#888' }}>{label}</div>
      <div style={{ fontSize: 28, fontWeight: 'bold', color: '#e6edf3' }}>{value}</div>
      <div style={{ fontSize: 12, color: '#888' }}>{unit}</div>
    </div>
  );
}

const REPORT_EVENTS = [
  { t: 0,   k:'INIT',       sev:'info',  m:'M8', d:'session attached · TRANSIT · D3 SUPERVISED' },
  { t: 25,  k:'T01_DET',    sev:'info',  m:'M2', d:'T01 detected · range 4.8 nm' },
  { t: 38,  k:'CPA_PROJ',   sev:'warn',  m:'M2', d:'T01 CPA projected 0.18 nm (below 0.40 nm threshold)' },
  { t: 47,  k:'SCENE_CHG',  sev:'info',  m:'M8', d:'TRANSIT → COLREG_AVOIDANCE' },
  { t: 49,  k:'COLREG_R14', sev:'info',  m:'M6', d:'Classification = HEAD-ON · GIVE-WAY · conf 0.91' },
  { t: 52,  k:'MPC_BRANCH', sev:'info',  m:'M5', d:'BC-MPC 5 branches · selected br#2 · STBD +40°' },
  { t: 67,  k:'AIS_DROP',   sev:'warn',  m:'M2', d:'T01 AIS dropout 30s · fallback radar tracking' },
  { t: 93,  k:'AIS_REC',    sev:'info',  m:'M2', d:'T01 AIS recovered · consistency ✓' },
  { t: 140, k:'CPA_MIN',    sev:'info',  m:'M2', d:'CPA_min @ T01 = 0.52 nm · passes safety threshold' },
  { t: 152, k:'SCENE_CHG',  sev:'info',  m:'M8', d:'COLREG_AVOIDANCE → TRANSIT' },
  { t: 218, k:'HB_LOSS',    sev:'crit',  m:'M7', d:'M7 heartbeat loss 2 frames · forcing ToR' },
  { t: 218, k:'ToR_REQ',    sev:'warn',  m:'M8', d:'ToR requested · D3 → D2 · 60s protocol' },
  { t: 224, k:'ToR_ACK',    sev:'info',  m:'M8', d:'Operator acknowledged · sat1=5.8s · D2' },
  { t: 224, k:'OVERRIDE',   sev:'warn',  m:'M8', d:'OVERRIDE_ACTIVE · M5 frozen · M7 degraded' },
  { t: 288, k:'HANDBACK',   sev:'info',  m:'M8', d:'Handback initiated · M7→M5 · 120ms done' },
  { t: 289, k:'TRANSIT',    sev:'info',  m:'M8', d:'OVERRIDE → TRANSIT · D2 → D3' },
  { t: 401, k:'GNSS_BIAS',  sev:'warn',  m:'M3', d:'GNSS bias +12m · EKF suppressed · negligible' },
  { t: 480, k:'WP_REACH',   sev:'info',  m:'M4', d:'WP-15 reached · error 38m' },
  { t: 600, k:'END',        sev:'info',  m:'M8', d:'run complete · verdict PASS · ASDR sealed' },
];

const ASDR_LEDGER_EVENTS = REPORT_EVENTS.map((e, i) => ({
  time: `T+${String(Math.floor(e.t / 60)).padStart(2, '0')}:${String(e.t % 60).padStart(2, '0')}`,
  type: e.k,
  module: e.m,
  payload: e.d,
  hash: `0xDEADBEEF${String(i).padStart(4, '0')}`,
}));

export function RunReport() {
  const scenarioId = useScenarioStore((s) => s.scenarioId);
  const storeRunId = useScenarioStore((s) => s.runId);
  const { data: scoring, refetch } = useGetLastRunScoringQuery();
  const runId = storeRunId || scoring?.run_id || scenarioId || 'latest';
  const [exportMarzip, { isLoading }] = useExportMarzipMutation();
  const [exportUrl, setExportUrl] = useState<string | null>(null);
  const [exportMsg, setExportMsg] = useState<string | null>(null);
  const [exportRequested, setExportRequested] = useState(false);
  const [verdict, setVerdict] = useState<'PASS' | 'FAIL' | null>(null);
  const [currentTimeSec, setCurrentTimeSec] = useState(0);
  const { data: exportStatus } = useGetExportStatusQuery(runId, {
    skip: !exportRequested || !!exportUrl,
    pollingInterval: 1500,
  } as any);

  useEffect(() => { refetch(); }, [refetch]);

  // Numeric formatters with hyphen fallback for missing data
  const fmt = (v: number | undefined, digits = 2) =>
    typeof v === 'number' ? v.toFixed(digits) : '—';
  const kpis = scoring?.kpis ?? null;
  const ruleChain = scoring?.rule_chain ?? [];

  const handleExport = async () => {
    setExportMsg(null);
    setExportRequested(true);
    try {
      const result = await exportMarzip(runId).unwrap();
      setExportMsg(result.status === 'processing'
        ? 'Marzip pack queued — link will appear when ready'
        : 'Marzip pack ready');
      if ((result as any).download_url) setExportUrl((result as any).download_url);
    } catch (e: any) {
      setExportMsg(`Export failed: ${e?.data?.detail || e?.error || 'unknown'}`);
      setExportRequested(false);
    }
  };

  // Promote download URL once the background task finishes
  useEffect(() => {
    if (exportStatus?.download_url && exportStatus.download_url !== exportUrl) {
      setExportUrl(exportStatus.download_url);
    }
  }, [exportStatus, exportUrl]);

  const handleVerdict = (v: 'PASS' | 'FAIL') => setVerdict(v);
  const handleNewRun = () => {
    // Reset client state so next run starts clean — Preflight will also call
    // POST /lifecycle/cleanup to bring backend FSM back to UNCONFIGURED
    useScenarioStore.getState().reset();
    window.location.hash = '#/builder';
  };

  return (
    <div style={{ height: '100%', display: 'flex', flexDirection: 'column', background: 'var(--bg-0)' }}>
      {/* Header + Actions */}
      <div style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'flex-end',
        padding: '16px 18px 0',
      }}>
        <div>
          <div style={{
            fontFamily: 'var(--f-disp)', fontSize: 16, color: 'var(--txt-0)',
            fontWeight: 700, letterSpacing: '0.16em', textTransform: 'uppercase',
          }}>RUN REPORT · {runId.slice(0, 8)}</div>
          <div style={{
            fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)', marginTop: 2,
          }}>
            scenario · seed · 600s · 2026-05-13 06:42:01 UTC
          </div>
        </div>
        <div style={{ display: 'flex', gap: 8 }}>
          <button onClick={() => window.location.hash = '#/bridge/latest'} style={{
            background: 'transparent', border: '1px solid var(--line-3)', color: 'var(--txt-1)',
            padding: '8px 14px', fontFamily: 'var(--f-disp)', fontSize: 10,
            letterSpacing: '0.16em', fontWeight: 600, cursor: 'pointer',
          }}>← BACK TO BRIDGE</button>
          <button onClick={() => {}} style={{
            background: 'transparent', border: '1px solid var(--c-phos)', color: 'var(--c-phos)',
            padding: '8px 14px', fontFamily: 'var(--f-disp)', fontSize: 10,
            letterSpacing: '0.16em', fontWeight: 600, cursor: 'pointer',
          }}>↓ EXPORT ASDR</button>
          <button onClick={() => { window.location.hash = '#/builder'; }} style={{
            background: 'var(--c-phos)', border: '1px solid var(--c-phos)', color: 'var(--bg-0)',
            padding: '8px 14px', fontFamily: 'var(--f-disp)', fontSize: 10,
            letterSpacing: '0.16em', fontWeight: 700, cursor: 'pointer',
          }}>NEW RUN →</button>
        </div>
      </div>

      {/* KPI Cards Row */}
      <div style={{
        display: 'flex', gap: 8, padding: '12px 18px', flexWrap: 'wrap',
      }}>
        {[
          {
            label: 'VERDICT',
            value: scoring?.verdict ? scoring.verdict.toUpperCase() : '—',
            sub: scoring?.verdict === 'pass' ? '✓ criteria met' : scoring?.verdict === 'fail' ? '✗ criteria failed' : 'pending',
            accent: scoring?.verdict === 'pass' ? 'var(--c-stbd)' : scoring?.verdict === 'fail' ? 'var(--c-danger)' : 'var(--txt-3)',
          },
          {
            label: 'Min CPA',
            value: kpis?.min_cpa_nm != null ? `${kpis.min_cpa_nm.toFixed(3)} nm` : '—',
            sub: '≥ 0.27 nm threshold',
            accent: kpis?.min_cpa_nm != null && kpis.min_cpa_nm >= 0.27 ? 'var(--c-phos)' : 'var(--c-danger)',
          },
          {
            label: 'TCPA Min',
            value: kpis?.tcpa_min_s != null ? `${kpis.tcpa_min_s.toFixed(0)} s` : '—',
            sub: 'time to min CPA',
            accent: 'var(--c-info)',
          },
          {
            label: 'Avg ROT',
            value: kpis?.avg_rot_dpm != null ? `${kpis.avg_rot_dpm.toFixed(1)} °/min` : '—',
            sub: 'mean rate of turn',
            accent: 'var(--c-info)',
          },
          {
            label: 'Max Rudder',
            value: kpis?.max_rudder_deg != null ? `${kpis.max_rudder_deg.toFixed(1)}°` : '—',
            sub: 'peak rudder angle',
            accent: kpis?.max_rudder_deg != null && kpis.max_rudder_deg <= 35 ? 'var(--c-stbd)' : 'var(--c-danger)',
          },
          {
            label: 'Grounding Risk',
            value: kpis?.grounding_risk_score != null ? `${(kpis.grounding_risk_score * 100).toFixed(1)}%` : '—',
            sub: 'min depth/draft ratio',
            accent: kpis?.grounding_risk_score != null && kpis.grounding_risk_score >= 0.9 ? 'var(--c-stbd)' : 'var(--c-danger)',
          },
          {
            label: 'Route Dev',
            value: kpis?.route_deviation_nm != null ? `${kpis.route_deviation_nm.toFixed(2)} nm` : '—',
            sub: 'max cross-track error',
            accent: 'var(--c-warn)',
          },
          {
            label: 'Time to MRC',
            value: kpis?.time_to_mrm_s != null && kpis.time_to_mrm_s > 0 ? `${kpis.time_to_mrm_s.toFixed(0)} s` : 'N/A',
            sub: kpis?.time_to_mrm_s != null && kpis.time_to_mrm_s > 0 ? 'MSO to MRC' : 'no MRC triggered',
            accent: 'var(--c-warn)',
          },
        ].map((kpi, i) => (
          <div key={i} style={{
            display: 'flex', flexDirection: 'column', gap: 2,
            padding: '6px 10px', borderLeft: '1px solid var(--line-1)',
            minWidth: 85,
          }}>
            <div style={{ fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--txt-3)',
                          textTransform: 'uppercase', letterSpacing: '0.16em' }}>
              {kpi.label}
            </div>
            <div style={{ display: 'flex', alignItems: 'baseline', gap: 4 }}>
              <span style={{ fontFamily: 'var(--f-mono)', fontSize: 16,
                             color: kpi.accent, fontWeight: 600 }}>
                {kpi.value}
              </span>
            </div>
            <div style={{ fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-2)' }}>
              {kpi.sub}
            </div>
          </div>
        ))}
      </div>

      {/* Main 2x2 Grid: Trajectory | ASDR, Timeline | Radar */}
      <div style={{
        flex: 1, display: 'grid', gridTemplateColumns: '1fr 1fr', gridTemplateRows: '1fr 220px',
        gap: 16, padding: '0 18px 12px', overflow: 'hidden',
      }}>
        <div className="glass-panel" style={{ gridColumn: '1', gridRow: '1', borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
          <TrajectoryReplay durationSec={600} currentTimeSec={currentTimeSec} />
        </div>
        
        <div className="glass-panel" style={{ gridColumn: '2', gridRow: '1', borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
          <AsdrLedger events={ASDR_LEDGER_EVENTS} />
        </div>
        
        <div className="glass-panel" style={{ gridColumn: '1', gridRow: '2', borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
          <TimelineSixLane
            events={REPORT_EVENTS}
            durationSec={600}
            currentTimeSec={currentTimeSec}
            onScrub={setCurrentTimeSec}
          />
        </div>
        
        <div className="glass-panel" style={{ gridColumn: '2', gridRow: '2', borderRadius: 8, overflow: 'hidden', display: 'flex', justifyContent: 'center', alignItems: 'center', padding: 16 }}>
          <ScoringRadarChart kpis={{
            safety: scoring?.scoring_dimensions?.safety ?? 0,
            ruleCompliance: scoring?.scoring_dimensions?.rule_compliance ?? 0,
            delay: Math.max(0, 1 - (scoring?.scoring_dimensions?.delay_penalty ?? 0)),
            magnitude: Math.max(0, 1 - (scoring?.scoring_dimensions?.action_magnitude_penalty ?? 0)),
            phase: scoring?.scoring_dimensions?.phase_score ?? 0,
            plausibility: scoring?.scoring_dimensions?.plausibility ?? 0,
          }} />
        </div>
      </div>
    </div>
  );
}
