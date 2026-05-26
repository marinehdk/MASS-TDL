import { memo } from 'react';
import { LucideChevronDown, LucideChevronUp } from 'lucide-react';
import type { FsmState, FsmTransition } from '../store/fsmStore';
import { FsmHistoryList } from './FsmHistoryList';

interface FsmStatePanelProps {
  state: FsmState;
  activeRule: string;
  confidence: number;
  history: FsmTransition[];
  expanded: boolean;
  onToggleExpand: () => void;
}

const FSM_DISPLAY: Record<FsmState, { icon: string; label: string; color: string }> = {
  TRANSIT: { icon: '🚢', label: 'TRANSIT', color: '#34d399' },
  COLREG_AVOIDANCE: { icon: '⚠️', label: 'COLREG AVOIDANCE', color: '#fbbf24' },
  TOR: { icon: '⛔', label: 'TOR (Operator)', color: '#f87171' },
  OVERRIDE: { icon: '🎮', label: 'OVERRIDE', color: '#f87171' },
  MRC: { icon: '🛑', label: 'EMERGENCY (MRC)', color: '#8b0000' },
  HANDBACK: { icon: '🔄', label: 'HANDBACK', color: '#06b6d4' },
};

export const FsmStatePanel = memo(function FsmStatePanel({
  state,
  activeRule,
  confidence,
  history,
  expanded,
  onToggleExpand,
}: FsmStatePanelProps) {
  const display = FSM_DISPLAY[state];
  const confPercent = Math.round(confidence * 100);
  const recent5 = history.slice(-5).reverse();

  return (
    <div style={{
      borderBottom: '1px solid var(--line-2)',
      padding: '6px 12px',
      background: display.color + '08',
    }}>
      <div style={{
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'center',
        marginBottom: expanded ? 8 : 0,
      }}>
        <span style={{
          fontFamily: 'var(--f-disp)',
          fontSize: 9,
          color: 'var(--c-phos)',
          letterSpacing: '0.1em',
          textTransform: 'uppercase',
        }}>
          ③ FSM State
        </span>
        <button
          onClick={onToggleExpand}
          style={{
            background: 'transparent',
            border: 'none',
            color: 'var(--c-text-2)',
            cursor: 'pointer',
            padding: '0 4px',
            display: 'flex',
            alignItems: 'center',
          }}
          title="Toggle FSM details"
        >
          {expanded ? <LucideChevronUp size={14} /> : <LucideChevronDown size={14} />}
        </button>
      </div>

      {expanded && (
        <div style={{ fontSize: 12, color: 'var(--c-text-1)', lineHeight: 1.6 }}>
          <div style={{
            display: 'flex',
            alignItems: 'center',
            gap: 8,
            marginBottom: 8,
            padding: 8,
            background: display.color + '12',
            borderRadius: 4,
            border: `1px solid ${display.color}`,
          }}>
            <span style={{ fontSize: 16 }}>{display.icon}</span>
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 'bold', color: display.color }}>
                {display.label}
              </div>
              <div style={{ fontSize: 10, color: 'var(--c-text-2)' }}>
                confidence {confPercent}%
              </div>
            </div>
          </div>

          <div style={{ marginBottom: 6 }}>
            <div style={{ fontSize: 9, color: 'var(--c-phos)', textTransform: 'uppercase', marginBottom: 2 }}>
              Active Rule
            </div>
            <div style={{
              padding: '4px 6px',
              background: 'var(--bg-2)',
              borderRadius: 2,
              fontFamily: 'var(--f-mono)',
              fontSize: 10,
              color: 'var(--c-info)',
              wordBreak: 'break-word',
            }}>
              {activeRule}
            </div>
          </div>

          {recent5.length > 0 && (
            <div style={{ marginTop: 6 }}>
              <div style={{ fontSize: 9, color: 'var(--c-phos)', textTransform: 'uppercase', marginBottom: 2 }}>
                History (Last 5)
              </div>
              <FsmHistoryList transitions={recent5} />
            </div>
          )}
        </div>
      )}
    </div>
  );
});
