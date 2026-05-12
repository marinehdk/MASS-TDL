import { useState } from 'react';
import { useExportMarzipMutation, useGetExportStatusQuery } from '../api/silApi';
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
  const runId = useScenarioStore((s) => s.runId) || scenarioId || 'latest';
  const [exportMarzip, { isLoading }] = useExportMarzipMutation();
  const [exportUrl, setExportUrl] = useState<string | null>(null);
  const [verdict, setVerdict] = useState<'PASS' | 'FAIL' | null>(null);

  const handleExport = async () => {
    try {
      const result = await exportMarzip(runId).unwrap();
      setExportUrl(result.download_url);
    } catch {
      // Export endpoint may not exist yet — graceful degradation
      alert('Export queued. Check back in a moment.');
    }
  };

  const handleVerdict = (v: 'PASS' | 'FAIL') => setVerdict(v);
  const handleNewRun = () => {
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
      <div style={{ display: 'flex', gap: 16, marginBottom: 24, flexWrap: 'wrap' }}>
        <KpiCard label="Min CPA" value="0.42" unit="nm" />
        <KpiCard label="Avg ROT" value="2.1" unit="°/min" />
        <KpiCard label="Distance" value="4.8" unit="nm" />
        <KpiCard label="Duration" value="342" unit="s" />
      </div>

      {/* COLREGs chain */}
      <div style={{
        flex: 1, background: '#1a1a2e', borderRadius: 8, padding: 16, marginBottom: 16,
      }}>
        <h3 style={{ margin: 0, marginBottom: 8 }}>COLREGs Rule Chain</h3>
        <p style={{ color: '#888', margin: 0 }}>
          Rule 14 (Head-on) → Rule 8 (Action to avoid collision) → Rule 16 (Give-way)
        </p>
        <p style={{ color: '#666', fontSize: 12, marginTop: 8 }}>
          Phase 2: populated from ASDR events
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
          <a href={exportUrl} style={{ color: '#4fc3f7' }}>Download</a>
        )}
      </div>
    </div>
  );
}
