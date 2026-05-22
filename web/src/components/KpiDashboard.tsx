import { useEffect, useState } from 'react';

interface KpiData {
  p95?: number;
  p99?: number;
  mean?: number;
  count?: number;
  algorithm_maturity?: { m4_ivp?: string; m5_bc_mpc?: string; note?: string };
  cells_lit?: number;
  total_cells?: number;
  first_run_pass_rate?: number;
  violation_rate?: number;
}

function StubBadge({ isStub }: { isStub: boolean }) {
  if (!isStub) return null;
  return (
    <span
      data-testid="stub-badge"
      style={{
        background: '#555', color: '#bbb', fontSize: 10,
        padding: '1px 5px', borderRadius: 3, marginLeft: 6,
        fontFamily: 'monospace', verticalAlign: 'middle',
      }}
    >
      [STUB]
    </span>
  );
}

function Panel({
  title, value, unit, threshold, isStub = false, pass,
}: {
  title: string; value?: string | number; unit?: string;
  threshold?: string; isStub?: boolean; pass?: boolean;
}) {
  const color = pass === undefined ? '#ccc' : pass ? '#4caf50' : '#f44336';
  return (
    <div style={{
      border: `1px solid ${color}`, borderRadius: 8, padding: 12,
      minWidth: 180, background: '#1a1a2e',
    }}>
      <div style={{ fontSize: 11, color: '#888', marginBottom: 4 }}>
        {title}<StubBadge isStub={isStub} />
      </div>
      <div style={{ fontSize: 20, color, fontWeight: 'bold' }}>
        {value ?? '—'}{unit && <span style={{ fontSize: 12, color: '#888' }}> {unit}</span>}
      </div>
      {threshold && (
        <div style={{ fontSize: 10, color: '#555', marginTop: 4 }}>threshold: {threshold}</div>
      )}
    </div>
  );
}

export function KpiDashboard({ orchestratorBase = 'http://localhost:8000' }: { orchestratorBase?: string }) {
  const [kpi, setKpi] = useState<KpiData>({});
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    const load = async () => {
      try {
        const r = await fetch(`${orchestratorBase}/api/v1/vv/kpi`);
        if (r.ok) setKpi(await r.json());
      } catch {
        setKpi({});
      }
      setLoading(false);
    };
    load();
    const interval = setInterval(load, 5000);
    return () => clearInterval(interval);
  }, [orchestratorBase]);

  if (loading) return <div>Loading KPI...</div>;

  const isStub = kpi.algorithm_maturity?.m4_ivp === 'stub';
  const p95Pass = kpi.p95 !== undefined ? kpi.p95 <= 800 : undefined;
  const p99Pass = kpi.p99 !== undefined ? kpi.p99 <= 1200 : undefined;
  const coveragePass = kpi.cells_lit !== undefined ? kpi.cells_lit >= 200 : undefined;
  const firstRunPass = kpi.first_run_pass_rate !== undefined ? kpi.first_run_pass_rate >= 0.9 : undefined;
  const colregsPass = kpi.violation_rate !== undefined ? kpi.violation_rate < 0.05 : undefined;

  return (
    <div data-testid="kpi-dashboard" style={{ display: 'flex', gap: 12, flexWrap: 'wrap', padding: 16 }}>
      {/* P1 Latency */}
      <div>
        <div style={{ color: '#888', fontSize: 11, marginBottom: 6 }}>P1 · Latency</div>
        <div style={{ display: 'flex', gap: 8 }}>
          <Panel title="E2E P95" value={kpi.p95?.toFixed(0)} unit="ms"
                 threshold="<= 800ms" isStub={isStub} pass={p95Pass} />
          <Panel title="E2E P99" value={kpi.p99?.toFixed(0)} unit="ms"
                 threshold="<= 1200ms" isStub={isStub} pass={p99Pass} />
        </div>
      </div>

      {/* P2 Decision Quality */}
      <div>
        <div style={{ color: '#888', fontSize: 11, marginBottom: 6 }}>P2 · Decision Quality</div>
        <Panel title="First-run pass rate" value={kpi.first_run_pass_rate !== undefined
          ? `${(kpi.first_run_pass_rate * 100).toFixed(1)}%` : undefined}
               threshold=">= 90%" isStub={isStub} pass={firstRunPass} />
      </div>

      {/* P3 Coverage */}
      <div>
        <div style={{ color: '#888', fontSize: 11, marginBottom: 6 }}>P3 · Coverage</div>
        <Panel title="Coverage cells" value={kpi.cells_lit} unit={`/ ${kpi.total_cells ?? 1100}`}
               threshold=">= 200 (Phase 2)" pass={coveragePass} />
      </div>

      {/* P4 COLREGs */}
      <div>
        <div style={{ color: '#888', fontSize: 11, marginBottom: 6 }}>P4 · COLREGs</div>
        <Panel title="Violation rate" value={kpi.violation_rate !== undefined
          ? `${(kpi.violation_rate * 100).toFixed(1)}%` : undefined}
               threshold="< 5%" isStub={isStub} pass={colregsPass} />
      </div>
    </div>
  );
}
