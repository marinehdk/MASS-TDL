import React from 'react';
import type { ModulePulse } from '../../types';

interface DecisionChainTimingBarProps {
  pulses: ModulePulse[];
}

const CHAIN_ORDER = [1, 2, 4, 6, 5, 7] as const;
const MODULE_THRESHOLD: Record<number, number> = { 5: 50 };
const DEFAULT_THRESHOLD = 20;

function segColor(moduleId: number, latencyMs: number): 'green' | 'amber' | 'red' {
  const threshold = MODULE_THRESHOLD[moduleId] ?? DEFAULT_THRESHOLD;
  if (latencyMs < 5) return 'green';
  if (latencyMs < threshold) return 'amber';
  return 'red';
}

const COLOR_VALUES = {
  green: '#34d399',
  amber: '#fbbf24',
  red:   '#f87171',
};

export const DecisionChainTimingBar: React.FC<DecisionChainTimingBarProps> = ({ pulses }) => {
  if (pulses.length === 0) return null;

  const byId: Record<number, ModulePulse> = {};
  for (const p of pulses) { if (p.moduleId != null) byId[Number(p.moduleId)] = p; }

  const segments = CHAIN_ORDER
    .filter((id) => byId[id] !== undefined)
    .map((id) => {
      const p = byId[id]!;
      const lat = p.latencyMs ?? 0;
      const color = segColor(id, lat);
      return { id, lat, color };
    });

  if (segments.length === 0) return null;

  const total = segments.reduce((sum, s) => sum + s.lat, 0);
  const isOverload = total > 100;

  return (
    <div
      data-testid="timing-bar"
      data-overload={String(isOverload)}
      style={{
        height: 24, background: isOverload ? 'rgba(248,81,73,0.15)' : 'var(--bg-1)',
        borderTop: `1px solid ${isOverload ? '#f87171' : 'var(--line-2)'}`,
        display: 'flex', alignItems: 'center', padding: '0 12px', gap: 4,
        fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)',
        flexShrink: 0,
      }}
    >
      {segments.map((seg, i) => (
        <React.Fragment key={seg.id}>
          <span
            data-testid={`timing-seg-${seg.id}`}
            data-color={seg.color}
            style={{ color: COLOR_VALUES[seg.color] }}
          >
            M{seg.id}[{seg.lat.toFixed(1)}ms]
          </span>
          {i < segments.length - 1 && (
            <span style={{ color: 'var(--txt-3)', opacity: 0.4 }}>→</span>
          )}
        </React.Fragment>
      ))}
      <span style={{ marginLeft: 8, color: 'var(--txt-3)', opacity: 0.6 }}>total:</span>
      <span
        data-testid="timing-total"
        style={{ color: isOverload ? '#f87171' : 'var(--txt-2)', fontWeight: 700 }}
      >
        {total.toFixed(1)}ms
      </span>
    </div>
  );
};
