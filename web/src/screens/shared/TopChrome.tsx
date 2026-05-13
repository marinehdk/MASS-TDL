import React from 'react';
import { RunStatePill } from './RunStatePill';
import { DualClock } from './DualClock';
import { useUIStore } from '../../store';
import { useTelemetryStore } from '../../store';

interface TopChromeProps {
  onNavigate?: (screen: 'builder' | 'preflight' | 'bridge' | 'report') => void;
}

const TABS = [
  { id: 'builder',  code: '01', label: 'SCENARIO',  zh: '场景编辑' },
  { id: 'preflight',code: '02', label: 'PRE-FLIGHT',zh: '飞行前检查' },
  { id: 'bridge',   code: '03', label: 'BRIDGE',    zh: '驾驶台运行' },
  { id: 'report',   code: '04', label: 'REPORT',    zh: '回放与报告' },
] as const;

type Screen = typeof TABS[number]['id'];

function getCurrentScreen(): Screen {
  const hash = window.location.hash.replace('#/', '');
  if (hash.startsWith('preflight')) return 'preflight';
  if (hash.startsWith('bridge')) return 'bridge';
  if (hash.startsWith('report')) return 'report';
  return 'builder';
}

export const TopChrome: React.FC<TopChromeProps> = ({ onNavigate }) => {
  const viewMode = useUIStore((s) => s.viewMode);
  const setViewMode = useUIStore((s) => s.setViewMode);
  const lifecycleStatus = useTelemetryStore((s) => s.lifecycleStatus);

  const currentScreen = getCurrentScreen();
  const runState: 'IDLE' | 'READY' | 'ACTIVE' | 'PAUSED' | 'COMPLETED' | 'ABORTED' =
    (lifecycleStatus?.current_state === 3 ? 'ACTIVE' :
     lifecycleStatus?.current_state === 2 ? 'READY' :
     lifecycleStatus?.current_state === 5 ? 'COMPLETED' :
     lifecycleStatus?.current_state === 4 ? 'PAUSED' :
     'IDLE') as any;
  const simTime = lifecycleStatus?.sim_time ?? 0;

  const handleNavigate = (id: Screen) => {
    if (onNavigate) {
      onNavigate(id);
    } else {
      window.location.hash = `#/${id}`;
    }
  };

  return (
    <div
      data-testid="top-chrome"
      style={{
        height: 48, background: 'var(--bg-1)', borderBottom: '1px solid var(--line-2)',
        display: 'flex', alignItems: 'stretch', flexShrink: 0, zIndex: 40,
        position: 'relative',
      }}
    >
      {/* Brand */}
      <div style={{
        display: 'flex', alignItems: 'center', gap: 10, padding: '0 16px',
        borderRight: '1px solid var(--line-2)', minWidth: 220,
      }}>
        <div style={{
          width: 24, height: 24, border: '1.5px solid var(--c-phos)',
          position: 'relative', display: 'flex', alignItems: 'center', justifyContent: 'center',
        }}>
          <div style={{
            width: 1, height: 10, background: 'var(--c-phos)',
            transformOrigin: 'bottom center', position: 'absolute', bottom: '50%',
            animation: 'radar-sweep 4s linear infinite',
          }} />
          <div style={{ width: 3, height: 3, background: 'var(--c-phos)', borderRadius: '50%' }} />
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', lineHeight: 1.15 }}>
          <span style={{
            fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-phos)',
            letterSpacing: '0.18em', fontWeight: 700, textTransform: 'uppercase',
          }}>MASS-L3 SIL</span>
          <span style={{ fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-3)' }}>
            TDL v1.1.2 · Session SIL-0427
          </span>
        </div>
      </div>

      {/* Nav Tabs */}
      <div style={{ display: 'flex', alignItems: 'stretch' }}>
        {TABS.map((t) => {
          const active = currentScreen === t.id;
          return (
            <div key={t.id} onClick={() => handleNavigate(t.id)} style={{
              display: 'flex', alignItems: 'center', gap: 8, padding: '0 14px', cursor: 'pointer',
              borderRight: '1px solid var(--line-1)',
              background: active ? 'var(--bg-0)' : 'transparent',
              borderBottom: active ? '2px solid var(--c-phos)' : '2px solid transparent',
            }}>
              <span style={{
                fontFamily: 'var(--f-mono)', fontSize: 9.5,
                color: active ? 'var(--c-phos)' : 'var(--txt-3)',
              }}>{t.code}</span>
              <div style={{ display: 'flex', flexDirection: 'column', lineHeight: 1.15 }}>
                <span style={{
                  fontFamily: 'var(--f-disp)', fontSize: 10.5,
                  color: active ? 'var(--txt-0)' : 'var(--txt-2)',
                  letterSpacing: '0.14em', fontWeight: 600, textTransform: 'uppercase',
                }}>{t.label}</span>
                <span style={{
                  fontFamily: 'var(--f-body)', fontSize: 8.5, color: 'var(--txt-3)',
                }}>{t.zh}</span>
              </div>
            </div>
          );
        })}
      </div>

      <div style={{ flex: 1 }} />

      {/* Right: State Pill + Dual Clock + View Toggle + REC */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '0 12px' }}>
        <RunStatePill state={runState} simTime={simTime} />
        <DualClock simTime={simTime} />

        {/* View Toggle — only on Bridge screen */}
        {currentScreen === 'bridge' && (
          <div data-testid="view-toggle" style={{
            display: 'flex', border: '1px solid var(--line-2)',
          }}>
            {(['captain', 'god'] as const).map((mode) => (
              <div key={mode} onClick={() => setViewMode(mode)} style={{
                padding: '3px 10px', cursor: 'pointer',
                background: viewMode === mode ? 'var(--c-phos)' : 'transparent',
                color: viewMode === mode ? 'var(--bg-0)' : 'var(--txt-3)',
                fontFamily: 'var(--f-disp)', fontSize: 9, letterSpacing: '0.14em',
                fontWeight: 600, textTransform: 'uppercase',
              }}>
                {mode}
              </div>
            ))}
          </div>
        )}

        {/* REC indicator */}
        {runState === 'ACTIVE' && (
          <div style={{ display: 'flex', alignItems: 'center', gap: 6, padding: '0 8px' }}>
            <div style={{
              width: 8, height: 8, borderRadius: '50%', background: 'var(--c-danger)',
              animation: 'phos-pulse 1.2s infinite',
            }} />
            <span style={{
              fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-danger)',
              letterSpacing: '0.16em', fontWeight: 600,
            }}>REC</span>
          </div>
        )}
      </div>
    </div>
  );
};
