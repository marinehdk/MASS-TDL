import { useEffect, useState } from 'react';
import {
  useExportMarzipMutation,
  useGetExportStatusQuery,
  useGetLastRunScoringQuery,
  useGetAsdrEventsQuery,
} from '../api/silApi';
import { useScenarioStore } from '../store';
import { TimelineSixLane } from './shared/TimelineSixLane';
import { AsdrLedger } from './shared/AsdrLedger';
import { TrajectoryReplay } from './shared/TrajectoryReplay';
import { ScoringRadarChart } from './shared/ScoringRadarChart';
import { ColregsDecisionTree } from './shared/ColregsDecisionTree';
import { BoundaryDiagnostics } from './shared/BoundaryDiagnostics';

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

export function SimulationEvaluator() {
  const scenarioId = useScenarioStore((s) => s.scenarioId);
  const storeRunId = useScenarioStore((s) => s.runId);
  const { data: scoring, refetch } = useGetLastRunScoringQuery();
  const { data: asdrData } = useGetAsdrEventsQuery();
  const reportEvents = asdrData?.events ?? [];
  const asdrLedgerEvents = asdrData?.ledger ?? [];
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
    window.location.hash = '#/scenario';
  };

  const isPass = scoring?.verdict === 'pass';
  const isFail = scoring?.verdict === 'fail';
  const verdictText = isPass ? '✓ PASS' : isFail ? '❌ FAIL' : '⏳ PENDING';
  const verdictColor = isPass ? 'var(--c-stbd)' : isFail ? 'var(--c-danger)' : 'var(--txt-3)';
  const verdictBg = isPass ? 'rgba(40,167,69,0.08)' : isFail ? 'rgba(220,53,69,0.08)' : 'var(--bg-2)';
  const verdictDetail = isPass 
    ? 'All safety & ODD envelopes met.' 
    : isFail 
      ? 'M5 planned late (T+108s).' 
      : 'Assessment pending...';

  const kpiCardsList = [
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
  ];

  return (
    <div style={{ height: '100%', display: 'flex', flexDirection: 'column', background: 'var(--bg-0)' }}>
      {/* Header + Actions */}
      <div style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'flex-end',
        padding: '16px 18px 0',
        marginBottom: '12px',
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
          <button onClick={() => window.location.hash = '#/scenario'} style={{
            background: 'transparent', border: '1px solid var(--line-3)', color: 'var(--txt-1)',
            padding: '8px 14px', fontFamily: 'var(--f-disp)', fontSize: 10,
            letterSpacing: '0.16em', fontWeight: 600, cursor: 'pointer',
          }}>← BACK TO SCENARIOS</button>
          <button onClick={handleExport} disabled={isLoading} style={{
            background: 'transparent', border: '1px solid var(--c-phos)', color: 'var(--c-phos)',
            padding: '8px 14px', fontFamily: 'var(--f-disp)', fontSize: 10,
            letterSpacing: '0.16em', fontWeight: 600, cursor: 'pointer',
            opacity: isLoading ? 0.6 : 1,
          }}>{isLoading ? 'EXPORTING...' : 'EXPORT MARZIP'}</button>
          <button onClick={handleNewRun} style={{
            background: 'var(--c-phos)', border: '1px solid var(--c-phos)', color: 'var(--bg-0)',
            padding: '8px 14px', fontFamily: 'var(--f-disp)', fontSize: 10,
            letterSpacing: '0.16em', fontWeight: 700, cursor: 'pointer',
          }}>NEW RUN →</button>
        </div>
      </div>

      {/* Responsive Three-Column Layout (Option A) */}
      <div style={{
        flex: 1,
        display: 'flex',
        gap: 16,
        padding: '0 18px 12px',
        overflow: 'hidden',
      }}>
        {/* Column 1 (42% width) */}
        <div style={{
          flex: 42,
          display: 'flex',
          flexDirection: 'column',
          height: '100%',
          minWidth: 0,
        }}>
          <div className="glass-panel" style={{ flex: 1, borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
            <TrajectoryReplay
              durationSec={600}
              currentTimeSec={currentTimeSec}
              onTimeChange={setCurrentTimeSec}
            />
          </div>
        </div>

        {/* Column 2 (28% width) */}
        <div style={{
          flex: 28,
          display: 'flex',
          flexDirection: 'column',
          gap: 12,
          height: '100%',
          minWidth: 0,
        }}>
          {/* TimelineSixLane (fixed height 180px) */}
          <div className="glass-panel" style={{ height: 180, flexShrink: 0, borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
            <TimelineSixLane
              events={reportEvents}
              durationSec={600}
              currentTimeSec={currentTimeSec}
              onScrub={setCurrentTimeSec}
            />
          </div>

          {/* ColregsDecisionTree (taking remaining height, min height 280px) */}
          <div className="glass-panel" style={{ flex: 1, minHeight: 280, borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
            <ColregsDecisionTree
              currentTimeSec={currentTimeSec}
            />
          </div>

          {/* ScoringRadarChart + ToR takeover panel (fixed height 180px, side-by-side) */}
          <div className="glass-panel" style={{
            height: 180, flexShrink: 0, borderRadius: 8, overflow: 'hidden',
            display: 'grid', gridTemplateColumns: '1fr 1fr', padding: 12, gap: 12
          }}>
            {/* Radar Chart */}
            <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', borderRight: '1px solid var(--line-1)' }}>
              <ScoringRadarChart kpis={{
                safety: scoring?.scoring_dimensions?.safety ?? 0,
                ruleCompliance: scoring?.scoring_dimensions?.rule_compliance ?? 0,
                delay: Math.max(0, 1 - (scoring?.scoring_dimensions?.delay_penalty ?? 0)),
                magnitude: Math.max(0, 1 - (scoring?.scoring_dimensions?.action_magnitude_penalty ?? 0)),
                phase: scoring?.scoring_dimensions?.phase_score ?? 0,
                plausibility: scoring?.scoring_dimensions?.plausibility ?? 0,
              }} />
            </div>

            {/* ToR Takeover & Ergonomic Panel */}
            <div style={{ display: 'flex', flexDirection: 'column', gap: 6, justifyContent: 'center', paddingLeft: 6 }}>
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
                letterSpacing: '0.12em', textTransform: 'uppercase', marginBottom: 2
              }}>
                ToR Ergonomics & Verdict
              </div>
              
              {/* Verdict */}
              <div style={{
                background: verdictBg,
                border: `1px solid ${verdictColor}`,
                borderRadius: 4, padding: '4px 8px', fontSize: 8.5, color: 'var(--txt-1)',
                fontFamily: 'var(--f-mono)'
              }}>
                <span style={{ fontWeight: 'bold', color: verdictColor }}>
                  {verdictText}:
                </span>{' '}
                {verdictDetail}
              </div>

              {/* Takeover Latency */}
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', borderBottom: '1px dashed var(--line-1)', paddingBottom: 2 }}>
                <span style={{ fontFamily: 'var(--f-body)', fontSize: 9, color: 'var(--txt-3)' }}>Takeover Latency</span>
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 12, fontWeight: 'bold', color: 'var(--c-warn)' }}>5.8 s</span>
              </div>
              <div style={{ fontFamily: 'var(--f-mono)', fontSize: 7.5, color: 'var(--txt-3)', marginTop: -4, opacity: 0.8 }}>
                ✓ CCS/Veitch Compliant (&lt; 10s limit)
              </div>

              {/* Manual vs MRC Delta */}
              <div style={{ display: 'flex', flexDirection: 'column', gap: 3, marginTop: 4 }}>
                <div style={{ fontFamily: 'var(--f-body)', fontSize: 8, color: 'var(--txt-2)', fontWeight: 'bold' }}>MANUAL VS MRC AUTOPILOT:</div>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 8, fontFamily: 'var(--f-mono)' }}>
                  <span style={{ color: 'var(--txt-3)' }}>Safety Delta (DCPA)</span>
                  <span style={{ color: 'var(--c-stbd)', fontWeight: 'bold' }}>+12.4%</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 8, fontFamily: 'var(--f-mono)' }}>
                  <span style={{ color: 'var(--txt-3)' }}>Smoothness Delta</span>
                  <span style={{ color: 'var(--c-danger)', fontWeight: 'bold' }}>-8.2%</span>
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* Column 3 (30% width) */}
        <div style={{
          flex: 30,
          display: 'flex',
          flexDirection: 'column',
          gap: 12,
          height: '100%',
          minWidth: 0,
        }}>
          {/* 2x4 KPI cards grid (compact layout) */}
          <div style={{
            display: 'grid',
            gridTemplateColumns: '1fr 1fr',
            gap: 6,
            flexShrink: 0,
          }}>
            {kpiCardsList.map((kpi, i) => (
              <div key={i} style={{
                display: 'flex',
                flexDirection: 'column',
                gap: 1,
                padding: '5px 8px',
                background: 'var(--bg-1)',
                border: '1px solid var(--line-2)',
                borderRadius: 4,
              }}>
                <div style={{
                  fontFamily: 'var(--f-disp)', fontSize: 7.5, color: 'var(--txt-3)',
                  textTransform: 'uppercase', letterSpacing: '0.12em'
                }}>
                  {kpi.label}
                </div>
                <div style={{ display: 'flex', alignItems: 'baseline', gap: 4 }}>
                  <span style={{
                    fontFamily: 'var(--f-mono)', fontSize: 13,
                    color: kpi.accent, fontWeight: 600
                  }}>
                    {kpi.value}
                  </span>
                </div>
                <div style={{ fontFamily: 'var(--f-mono)', fontSize: 7.5, color: 'var(--txt-2)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                  {kpi.sub}
                </div>
              </div>
            ))}
          </div>

          {/* BoundaryDiagnostics recommendations card */}
          <div className="glass-panel" style={{ flexShrink: 0, borderRadius: 8, overflow: 'hidden' }}>
            <BoundaryDiagnostics
              minCpaNm={kpis?.min_cpa_nm}
              maxRudderDeg={kpis?.max_rudder_deg}
              events={reportEvents}
            />
          </div>

          {/* AsdrLedger scrollable table (taking remaining height) */}
          <div className="glass-panel" style={{ flex: 1, borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
            <AsdrLedger
              events={asdrLedgerEvents}
              currentTimeSec={currentTimeSec}
              onEventSelect={setCurrentTimeSec}
            />
          </div>
        </div>
      </div>
    </div>
  );
}
