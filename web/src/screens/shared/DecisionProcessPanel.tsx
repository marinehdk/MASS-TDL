import React from 'react';
import type { SAT2Data, SotifMetrics } from '../../types/sat';
import { ColregsRationaleTree } from './ColregsRationaleTree';
import { SotifMonitorStrip } from './SotifMonitorStrip';
import type { AvoidancePhaseState } from './avoidancePhase';

interface DecisionProcessPanelProps {
  phaseState: AvoidancePhaseState;
  sat2: SAT2Data | null;
  sotifMetrics: SotifMetrics | null;
  safetyAlert: { recommendedMrm?: string | null } | null;
}

const MODULE_COLORS: Record<string, string> = {
  M2: '#5bc0be',
  M6: '#fbbf24',
  M4: '#60a5fa',
  M5: '#34d399',
  M7: '#f87171',
};

function fmtNm(value: number | null): string {
  return typeof value === 'number' ? `${value.toFixed(2)} nm` : '—';
}

function fmtMin(value: number | null): string {
  return typeof value === 'number' ? `${value.toFixed(1)} min` : '—';
}

function fmtDeg(value: number | null): string {
  return typeof value === 'number' ? `${value.toFixed(1)}°` : '—';
}

function fmtNumber(value: number | null, digits = 2): string {
  return typeof value === 'number' ? value.toFixed(digits) : '—';
}

function fmtPercent(value: number | null): string {
  return typeof value === 'number' ? `${(value * 100).toFixed(0)}%` : '—';
}

const fieldStyle: React.CSSProperties = {
  display: 'flex',
  justifyContent: 'space-between',
  gap: 10,
  fontFamily: 'var(--f-mono)',
  fontSize: 10,
  lineHeight: 1.35,
};

function Field({ label, value }: { label: string; value: React.ReactNode }) {
  return (
    <div style={fieldStyle}>
      <span style={{ color: 'var(--txt-3)' }}>{label}</span>
      <span style={{ color: value === '—' ? 'var(--txt-3)' : 'var(--txt-1)', fontWeight: 700, textAlign: 'right' }}>
        {value}
      </span>
    </div>
  );
}

function ModuleBlock({
  id,
  title,
  active,
  children,
}: {
  id: 'M2' | 'M6' | 'M4' | 'M5' | 'M7';
  title: string;
  active: boolean;
  children: React.ReactNode;
}) {
  const color = MODULE_COLORS[id];
  return (
    <section
      data-testid={`decision-process-${id}`}
      data-active={String(active)}
      style={{
        borderLeft: `3px solid ${active ? color : 'var(--line-2)'}`,
        borderBottom: '1px solid var(--line-2)',
        padding: '9px 12px',
        background: active ? `${color}12` : 'transparent',
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 7 }}>
        <span style={{ color, fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 800, letterSpacing: '0.08em' }}>
          {id} {title}
        </span>
        <span style={{ color: active ? color : 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 9 }}>
          {active ? 'ACTIVE' : 'STANDBY'}
        </span>
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 5 }}>
        {children}
      </div>
    </section>
  );
}

function chainInputsText(sat2: SAT2Data | null): string | null {
  const entries = (sat2?.colregs_chain ?? [])
    .flatMap((layer) => Object.entries(layer.inputs ?? {}))
    .slice(0, 6);
  if (entries.length === 0) return null;
  return entries.map(([key, value]) => `${key}: ${value}`).join(' · ');
}

function completeSotifMetrics(metrics: SotifMetrics | null): SotifMetrics | null {
  if (!metrics) return null;
  const values = [
    metrics.ais_radar_consistency_sigma,
    metrics.target_predictability_rms_m,
    metrics.perception_coverage_pct,
    metrics.colregs_parse_failures,
    metrics.comm_link_rtt_ms,
    metrics.checker_veto_rate_pct,
  ];
  return values.every((value) => typeof value === 'number' && Number.isFinite(value)) ? metrics : null;
}

export const DecisionProcessPanel: React.FC<DecisionProcessPanelProps> = ({
  phaseState,
  sat2,
  sotifMetrics,
  safetyAlert,
}) => {
  const active = new Set(phaseState.activeModules);
  const { chain } = phaseState;
  const m6Inputs = chainInputsText(sat2);
  const completeSotif = completeSotifMetrics(sotifMetrics);

  return (
    <div data-testid="decision-process-panel" style={{
      display: 'flex',
      flexDirection: 'column',
      background: 'rgba(10, 15, 24, 0.96)',
      border: '1px solid var(--line-2)',
      borderRadius: 8,
      overflow: 'hidden',
      color: 'var(--txt-1)',
      width: '100%',
    }}>
      <header style={{ padding: '12px 14px', borderBottom: '1px solid var(--line-2)', background: 'rgba(91,192,190,0.08)' }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 12 }}>
          <span style={{ color: 'var(--c-phos)', fontFamily: 'var(--f-disp)', fontSize: 13, fontWeight: 900, letterSpacing: '0.08em' }}>
            避碰过程
          </span>
          <span data-testid="decision-process-phase" style={{ color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', fontSize: 10, fontWeight: 800 }}>
            {phaseState.phaseLabel}
          </span>
        </div>
        <div style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10, marginTop: 5 }}>
          {phaseState.phaseReason}
        </div>
      </header>

      <div style={{
        display: 'grid',
        gridTemplateColumns: 'repeat(5, minmax(0, 1fr))',
        gap: 4,
        padding: '8px 10px',
        borderBottom: '1px solid var(--line-2)',
        fontFamily: 'var(--f-mono)',
        fontSize: 9,
      }}>
        {(['M2', 'M6', 'M4', 'M5', 'M7'] as const).map((id) => (
          <div key={id} style={{
            textAlign: 'center',
            color: active.has(id) ? MODULE_COLORS[id] : 'var(--txt-3)',
            fontWeight: active.has(id) ? 900 : 600,
          }}>
            {id}
          </div>
        ))}
      </div>

      <ModuleBlock id="M2" title="态势" active={active.has('M2')}>
        <Field label="目标" value={chain.m2.nearestTargetId ?? '—'} />
        <Field label="RNG" value={fmtNm(chain.m2.rangeNm)} />
        <Field label="BRG" value={fmtDeg(chain.m2.bearingDeg)} />
        <Field label="CPA" value={fmtNm(chain.m2.cpaNm)} />
        <Field label="TCPA" value={fmtMin(chain.m2.tcpaMin)} />
        <Field label="Encounter" value={chain.m2.encounter ?? '—'} />
      </ModuleBlock>

      <ModuleBlock id="M6" title="规则" active={active.has('M6')}>
        <Field label="Rule" value={chain.m6.rule} />
        <Field label="Role" value={chain.m6.role} />
        <Field label="Direction" value={chain.m6.preferredDirection} />
        <Field label="Alter" value={chain.m6.minAlterationDeg !== null ? `${chain.m6.minAlterationDeg.toFixed(0)}°` : '—'} />
        <Field label="Phase" value={chain.m6.phase} />
        <Field label="M2 几何" value={m6Inputs ?? `${fmtDeg(chain.m2.bearingDeg)} / ${fmtNm(chain.m2.rangeNm)} / ${fmtNm(chain.m2.cpaNm)} / ${fmtMin(chain.m2.tcpaMin)}`} />
        {sat2?.colregs_chain?.length ? (
          <div style={{ marginTop: 6, border: '1px solid var(--line-2)' }}>
            <ColregsRationaleTree
              chain={sat2.colregs_chain}
              targetId={sat2.colregs_chain_target_id}
              latencyMs={sat2.reasoning_latency_ms}
            />
          </div>
        ) : null}
      </ModuleBlock>

      <ModuleBlock id="M4" title="仲裁" active={active.has('M4')}>
        <Field label="Behavior" value={chain.m4.behavior} />
        <Field label="HDG Window" value={chain.m4.headingWindow} />
        <Field label="SPD Window" value={chain.m4.speedWindow} />
        <Field label="Confidence" value={fmtPercent(chain.m4.confidence)} />
        <Field label="方向代价" value={(sat2?.ivp_contributions?.length ?? 0) > 0 ? `${sat2!.ivp_contributions.length} dirs` : '—'} />
      </ModuleBlock>

      <ModuleBlock id="M5" title="规划" active={active.has('M5')}>
        <Field label="Status" value={chain.m5.status} />
        <Field label="Waypoints" value={String(chain.m5.waypointCount)} />
        <Field label="Horizon" value={chain.m5.horizonS !== null ? `${chain.m5.horizonS.toFixed(0)} s` : '—'} />
        <Field label="候选轨迹" value={String(chain.m5.candidateCount)} />
        <Field label="Optimal Cost" value={fmtNumber(chain.m5.optimalCandidateCost)} />
      </ModuleBlock>

      <ModuleBlock id="M7" title="安全" active={active.has('M7')}>
        <Field label="Severity" value={chain.m7.severity} />
        <Field label="MRM" value={chain.m7.recommendedMrm ?? '—'} />
        <Field label="Violations" value={String(chain.m7.violatedMetricCount)} />
        <Field label="Description" value={chain.m7.description} />
        <div style={{ marginTop: 6, border: '1px solid var(--line-2)' }}>
          <SotifMonitorStrip
            metrics={completeSotif}
            recommendedMrm={safetyAlert?.recommendedMrm ?? undefined}
          />
        </div>
      </ModuleBlock>
    </div>
  );
};
