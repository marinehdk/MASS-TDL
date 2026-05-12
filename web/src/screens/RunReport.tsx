import { useEffect, useState } from 'react';
import {
  useExportMarzipMutation,
  useGetExportStatusQuery,
  useGetLastRunScoringQuery,
} from '../api/silApi';
import { useScenarioStore } from '../store';

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
    <div style={{
      padding: 24, height: '100vh', display: 'flex', flexDirection: 'column',
      background: '#0b1320', color: '#e6edf3',
    }} data-testid="run-report">
      {/* Timeline scrubber stub */}
      <div style={{
        height: 80, background: '#1a1a2e', borderRadius: 8, marginBottom: 16,
        display: 'flex', alignItems: 'center', justifyContent: 'center',
      }}>
        <span style={{ color: '#888' }}>Timeline scrub — Phase 2</span>
      </div>

      {/* KPI cards */}
      <div style={{ display: 'flex', gap: 16, marginBottom: 24, flexWrap: 'wrap' }}
           data-testid="run-report-kpis">
        <KpiCard label="Min CPA" value={fmt(kpis?.min_cpa_nm, 2)} unit="nm" />
        <KpiCard label="Avg ROT" value={fmt(kpis?.avg_rot_dpm, 1)} unit="°/min" />
        <KpiCard label="Distance" value={fmt(kpis?.distance_nm, 1)} unit="nm" />
        <KpiCard label="Duration" value={fmt(kpis?.duration_s, 0)} unit="s" />
      </div>

      {/* COLREGs chain */}
      <div style={{
        flex: 1, background: '#1a1a2e', borderRadius: 8, padding: 16, marginBottom: 16,
      }} data-testid="rule-chain">
        <h3 style={{ margin: 0, marginBottom: 8 }}>COLREGs Rule Chain</h3>
        <p style={{ color: '#888', margin: 0 }}>
          {ruleChain.length > 0 ? ruleChain.join(' → ') : 'No rule events captured'}
        </p>
        <p style={{ color: '#666', fontSize: 12, marginTop: 8 }}>
          {ruleChain.length > 0
            ? `Run ID: ${runId}`
            : 'Phase 2: populated from ASDR events via scoring_node'}
        </p>
      </div>

      {/* Verdict + Export + New Run */}
      <div style={{ display: 'flex', gap: 16, alignItems: 'center' }}>
        <button
          onClick={() => handleVerdict('PASS')}
          data-testid="btn-pass"
          style={{
            background: verdict === 'PASS' ? '#34d399' : '#333',
            color: '#fff', border: 'none', padding: '8px 24px', borderRadius: 4,
            cursor: 'pointer',
          }}>
          PASS
        </button>
        <button
          onClick={() => handleVerdict('FAIL')}
          data-testid="btn-fail"
          style={{
            background: verdict === 'FAIL' ? '#f87171' : '#333',
            color: '#fff', border: 'none', padding: '8px 24px', borderRadius: 4,
            cursor: 'pointer',
          }}>
          FAIL
        </button>
        <button
          onClick={handleExport}
          disabled={isLoading}
          data-testid="btn-export"
          style={{
            background: '#333', color: '#fff', border: 'none',
            padding: '8px 24px', borderRadius: 4, cursor: 'pointer',
          }}>
          {isLoading ? 'Exporting...' : 'Export .bag / .yaml / .csv'}
        </button>
        <button
          onClick={handleNewRun}
          data-testid="btn-new-run"
          style={{
            background: '#2dd4bf', color: '#0b1320', border: 'none',
            padding: '8px 24px', borderRadius: 4, cursor: 'pointer',
            marginLeft: 'auto',
          }}>
          New Run
        </button>
        {exportUrl && (
          <a href={exportUrl} style={{ color: '#4fc3f7' }} data-testid="download-link">Download</a>
        )}
      </div>
      {exportMsg && (
        <div style={{ marginTop: 12, color: '#4fc3f7', fontFamily: 'monospace', fontSize: 12 }}
             data-testid="export-msg">
          {exportMsg}
        </div>
      )}
      {verdict && (
        <div style={{ marginTop: 8, color: verdict === 'PASS' ? '#34d399' : '#f87171', fontSize: 13 }}
             data-testid="verdict">
          Verdict: {verdict}
        </div>
      )}
    </div>
  );
}
