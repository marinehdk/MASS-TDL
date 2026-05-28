import React from 'react';
import type { ColregsChainLayer } from '../../types/sat';

interface ColregsRationaleTreeProps {
  chain: ColregsChainLayer[];
  targetId: string | null;
  latencyMs: number;
}

const LAYER_LABELS = ['ODD', '会遇分类', '责任', '行动方向', '时机'] as const;
const STAGE_COLORS: Record<string, string> = {
  STAGE_1: '#34d399',
  STAGE_2: '#fbbf24',
  STAGE_3: '#f87171',
  EMERGENCY: '#8b0000',
};

export const ColregsRationaleTree: React.FC<ColregsRationaleTreeProps> = ({ chain, targetId, latencyMs }) => {
  if (chain.length === 0) {
    return (
      <div style={{ padding: '16px 12px', fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-3)' }}>
        No active COLREGs encounter
      </div>
    );
  }

  return (
    <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)' }}>
      {/* Header */}
      <div style={{
        padding: '6px 12px', background: 'var(--bg-0)',
        borderBottom: '1px solid var(--line-2)',
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
      }}>
        <span style={{ color: 'var(--c-phos)', fontWeight: 700, fontSize: 10, letterSpacing: '0.1em' }}>
          M6 COLREGs REASONING
        </span>
        <span style={{ color: 'var(--txt-3)', fontSize: 9 }}>
          {targetId ? `MMSI:${targetId}` : ''} · {latencyMs.toFixed(1)}ms
        </span>
      </div>

      {/* 5 Layers */}
      {chain.map((layer) => {
        const isEscalation = layer.escalation === true;
        const stageColor = layer.timing_stage ? STAGE_COLORS[layer.timing_stage] : undefined;

        return (
          <div
            key={layer.layer}
            data-testid={`colregs-layer-${layer.layer}`}
            data-escalation={isEscalation ? 'true' : undefined}
            style={{
              padding: '8px 12px',
              borderBottom: '1px solid var(--line-2)',
              background: isEscalation ? 'rgba(248,81,73,0.06)' : 'transparent',
              borderLeft: `3px solid ${stageColor ?? (isEscalation ? '#f87171' : 'var(--line-2)')}`,
            }}
          >
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 2 }}>
              <span style={{ color: 'var(--txt-3)', fontSize: 9, letterSpacing: '0.08em', textTransform: 'uppercase' }}>
                [{layer.layer}] {LAYER_LABELS[layer.layer - 1] ?? layer.label}
              </span>
              {layer.confidence !== undefined && (
                <span style={{ color: 'var(--txt-3)', fontSize: 9 }}>
                  conf: {(layer.confidence * 100).toFixed(0)}%
                </span>
              )}
            </div>
            <div style={{ color: stageColor ?? 'var(--txt-0)', fontWeight: 700, fontSize: 12 }}>
              {layer.conclusion}
              {isEscalation && (
                <span style={{ color: '#f87171', fontSize: 9, marginLeft: 6 }}>⚠️ 独立避让</span>
              )}
            </div>
            {/* Key input values */}
            <div style={{ color: 'var(--txt-3)', fontSize: 9, marginTop: 2 }}>
              {Object.entries(layer.inputs ?? {})
                .map(([k, v]) => `${k}: ${v}`)
                .join(' · ')}
            </div>
          </div>
        );
      })}
    </div>
  );
};
