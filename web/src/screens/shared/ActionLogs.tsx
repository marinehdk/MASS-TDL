import React, { useState } from 'react';
import type { GateSSEEvent } from '../../types/gateStream';
import { LiveLogStream } from './LiveLogStream';
import { QuickFixPanel } from './QuickFixPanel';
import { ContainerSpecPanel } from './ContainerSpecPanel';

const GATE_FILTER_MAP: Record<number, string> = {
  1: 'foxglove|docker',
  2: 'm7_safety',
  3: 'scenario|odd',
  4: 'scenario|odd',
  5: 'clock|chrony',
  6: 'm7|cgroup',
};

interface ActionLogsProps {
  focusedGateId: number | null;
  gates: GateSSEEvent[];
  scenarioId: string | null;
  runId: string | null;
  onRerun: () => void;
  onAbort: () => void;
  onFixApplied: () => void;
  isDev?: boolean;
  devSkipReason?: string;
  onDevSkipReasonChange?: (reason: string) => void;
  onDevSkip?: () => void;
}

// Collapsible Section matching Screen 1's premium architecture
interface CollapsibleSectionProps {
  title: string;
  children: React.ReactNode;
  defaultExpanded?: boolean;
}

function CollapsibleSection({
  title,
  children,
  defaultExpanded = true
}: CollapsibleSectionProps) {
  const [isExpanded, setIsExpanded] = useState(defaultExpanded);
  return (
    <div style={{
      background: 'rgba(16, 27, 44, 0.4)',
      border: '1px solid var(--line-1)',
      borderRadius: 8,
      overflow: 'hidden',
      flexShrink: 0
    }}>
      <div
        onClick={() => setIsExpanded(!isExpanded)}
        style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          padding: '12px 16px',
          background: isExpanded ? 'rgba(91,192,190,0.08)' : 'transparent',
          cursor: 'pointer',
          borderBottom: isExpanded ? '1px solid var(--line-1)' : 'none',
          transition: 'all 0.2s'
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <div style={{ width: 5, height: 15, background: 'var(--c-phos)', borderRadius: 2 }} />
          <span style={{
            fontSize: 14,
            fontWeight: 700,
            color: 'var(--txt-1)',
            fontFamily: 'var(--f-disp)',
            letterSpacing: '0.1em'
          }}>
            {title}
          </span>
        </div>
        <span style={{
          color: 'var(--txt-3)',
          transform: isExpanded ? 'rotate(90deg)' : 'none',
          transition: 'transform 0.2s',
          fontSize: 10
        }}>
          ▶
        </span>
      </div>
      {isExpanded && (
        <div style={{ padding: 16 }}>
          {children}
        </div>
      )}
    </div>
  );
}

export function ActionLogs({
  focusedGateId, gates, scenarioId, runId, onRerun, onAbort, onFixApplied,
  isDev, devSkipReason, onDevSkipReasonChange, onDevSkip
}: ActionLogsProps) {
  const nodeFilter = focusedGateId ? GATE_FILTER_MAP[focusedGateId] : undefined;
  const failedCount = gates.filter(g => !g.passed).length;
  const isQuickFixAvailable = focusedGateId && gates.some(g => !g.passed);

  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      background: 'rgba(10, 15, 24, 0.95)',
      backdropFilter: 'blur(16px)',
      borderLeft: '1px solid var(--line-2)',
      padding: '20px 0 10px',
      overflow: 'hidden'
    }}>
      {/* Centered Brand Header matching Left Sidebar */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8, padding: '0 16px 14px', borderBottom: '1px solid var(--line-1)', flexShrink: 0 }}>
        <div style={{ width: 4, height: 14, background: 'var(--c-phos)', borderRadius: 2 }} />
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 15, fontWeight: 700, color: 'var(--txt-1)', letterSpacing: '0.2em' }}>
          检测详情
        </span>
        {failedCount > 0 && (
          <span style={{ fontSize: 10, fontFamily: 'var(--f-mono)', padding: '2px 6px', borderRadius: 3, background: 'rgba(248,81,73,0.15)', color: 'var(--c-danger)', marginLeft: 6 }}>
            {failedCount} FAIL
          </span>
        )}
      </div>

      {/* Unified Scrollable Central Wrapper to absolutely prevent overlaps */}
      <div style={{
        flex: 1,
        overflowY: 'auto',
        padding: '6px 16px 20px',
        display: 'flex',
        flexDirection: 'column',
        gap: 16
      }}>
        {/* Card 1: 容器详情 (Only displayed when Gate 1 or Gate 2 is selected) */}
        {(focusedGateId === 1 || focusedGateId === 2) && (
          <CollapsibleSection title="容器详情" defaultExpanded={true}>
            <ContainerSpecPanel focusedGateId={focusedGateId} />
          </CollapsibleSection>
        )}

        {/* Card 2: 实时日志 (Always display in a height-constrained console wrapper inside the card) */}
        <CollapsibleSection title="实时日志" defaultExpanded={true}>
          <div style={{ height: 260, border: '1px solid var(--line-1)', borderRadius: 4, overflow: 'hidden' }}>
            <LiveLogStream nodeFilter={nodeFilter} maxLines={150} />
          </div>
        </CollapsibleSection>

        {/* Card 3: 快速修复 (Only displayed if the focused gate is failing) */}
        {isQuickFixAvailable && (
          <CollapsibleSection title="快速修复" defaultExpanded={true}>
            <QuickFixPanel
              focusedGateId={focusedGateId}
              gates={gates}
              scenarioId={scenarioId}
              runId={runId}
              onFixApplied={onFixApplied}
              isEmbed={true}
            />
          </CollapsibleSection>
        )}
      </div>

      {/* Developer Safety Bypass Panel */}
      {isDev && onDevSkip && onDevSkipReasonChange && (
        <div style={{
          margin: '0 16px 10px',
          padding: '12px 14px',
          border: '1px dashed var(--c-warn)',
          borderRadius: 8,
          background: 'rgba(240, 183, 47, 0.05)',
          display: 'flex',
          flexDirection: 'column',
          gap: 8,
          flexShrink: 0
        }}>
          <div style={{ fontFamily: 'var(--f-disp)', fontSize: 13, fontWeight: 700, color: 'var(--c-warn)', letterSpacing: '0.05em' }}>
            开发跳过
          </div>
          <div style={{ display: 'flex', gap: 8 }}>
            <input 
              value={devSkipReason ?? ''} 
              onChange={e => onDevSkipReasonChange(e.target.value)}
              placeholder="请说明强制跳过原因..." 
              style={{ 
                flex: 1, 
                padding: '6px 10px', 
                border: '1px solid var(--line-1)', 
                borderRadius: 4, 
                background: 'rgba(0,0,0,0.4)', 
                color: 'var(--txt-1)', 
                fontSize: 12,
                fontFamily: 'var(--f-body)',
                outline: 'none'
              }} 
            />
            <button 
              onClick={onDevSkip} 
              disabled={!(devSkipReason ?? '').trim()}
              style={{ 
                padding: '6px 12px', 
                background: 'var(--c-warn)', 
                color: '#000', 
                border: 'none', 
                borderRadius: 4, 
                cursor: (devSkipReason ?? '').trim() ? 'pointer' : 'not-allowed', 
                fontFamily: 'var(--f-disp)', 
                fontSize: 12,
                fontWeight: 700,
                opacity: (devSkipReason ?? '').trim() ? 1 : 0.5,
                transition: 'all 0.15s'
              }}
            >
              强制跳过
            </button>
          </div>
        </div>
      )}

      {/* Sticky Bottom Actions */}
      <div style={{ padding: '10px 16px 0', display: 'flex', gap: 8, borderTop: '1px solid var(--line-2)', flexShrink: 0 }}>
        <button onClick={onRerun} style={{
          flex: 1, padding: '10px 12px', border: 'none', borderRadius: 4, cursor: 'pointer',
          background: 'var(--c-phos)', color: '#000', fontFamily: 'var(--f-disp)', fontSize: 12, fontWeight: 700,
          transition: 'all 0.15s'
        }}
          onMouseOver={(e) => e.currentTarget.style.filter = 'brightness(1.1)'}
          onMouseOut={(e) => e.currentTarget.style.filter = 'none'}
        >
          重新检查
        </button>
        <button onClick={onAbort} style={{
          flex: 1, padding: '10px 12px', border: '1px solid var(--c-danger)', borderRadius: 4, cursor: 'pointer',
          background: 'transparent', color: 'var(--c-danger)', fontFamily: 'var(--f-disp)', fontSize: 12, fontWeight: 700,
          transition: 'all 0.15s'
        }}
          onMouseOver={(e) => e.currentTarget.style.background = 'rgba(248,81,73,0.08)'}
          onMouseOut={(e) => e.currentTarget.style.background = 'transparent'}
        >
          返回场景
        </button>
      </div>
    </div>
  );
}
