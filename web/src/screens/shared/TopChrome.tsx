import React from 'react';
import { RunStatePill } from './RunStatePill';
import { DualClock } from './DualClock';
import { useUIStore } from '../../store';
import { useTelemetryStore } from '../../store';
import { LucideCompass } from 'lucide-react';

interface TopChromeProps {
  onNavigate?: (screen: 'builder' | 'preflight' | 'bridge' | 'report') => void;
}

const TABS = [
  { id: 'builder',  code: '01', label: 'SIMULATION SCENARIO', zh: '仿真场景', desc: '负责仿真场景的内容展示' },
  { id: 'preflight',code: '02', label: 'SIMULATION CHECK',    zh: '仿真检查', desc: '负责检查目前TDL系统中各模块是否正常，前后端接口是否正常' },
  { id: 'bridge',   code: '03', label: 'SIMULATION BRIDGE',   zh: '仿真运行', desc: '负责接收TDL输出的状态和信息，模拟真实航行中的内容，并通过界面展示态势信息和决策等信息' },
  { id: 'report',   code: '04', label: 'SIMULATION REPORT',   zh: '仿真报告', desc: '负责对此次运行进行评价，是否完成了规避，各种指标如何，以及对完整的仿真数据进行记录和回放管理' },
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
        height: 60, background: 'var(--bg-1)', borderBottom: '1px solid var(--line-2)',
        display: 'flex', alignItems: 'stretch', flexShrink: 0, zIndex: 40,
        position: 'relative',
      }}
    >
      {/* Brand */}
      <div style={{
        width: 'auto', minWidth: 200, boxSizing: 'border-box',
        display: 'flex', alignItems: 'center', gap: 12, padding: '0 20px',
        borderRight: '1px solid var(--line-2)',
      }}>
        <div style={{ 
          width: 40, height: 40, borderRadius: '50%', 
          background: '#070c13', 
          border: '1px solid rgba(91, 192, 190, 0.3)',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          boxShadow: '0 0 15px rgba(91, 192, 190, 0.2)', flexShrink: 0,
          position: 'relative', overflow: 'hidden'
        }}>
          <svg width="32" height="32" viewBox="0 0 100 100" fill="none">
            {/* Crosshairs */}
            <line x1="50" y1="10" x2="50" y2="90" stroke="rgba(91, 192, 190, 0.2)" strokeWidth="0.5" />
            <line x1="10" y1="50" x2="90" y2="50" stroke="rgba(91, 192, 190, 0.2)" strokeWidth="0.5" />
            
            {/* Concentric Circles */}
            <circle cx="50" cy="50" r="15" stroke="rgba(91, 192, 190, 0.3)" strokeWidth="0.5" strokeDasharray="2 2" />
            <circle cx="50" cy="50" r="30" stroke="rgba(91, 192, 190, 0.3)" strokeWidth="0.5" strokeDasharray="2 2" />
            <circle cx="50" cy="50" r="44" stroke="rgba(91, 192, 190, 0.15)" strokeWidth="0.5" strokeDasharray="2 2" />
            
            {/* Scan Sector */}
            <path d="M50 50 L82 18 A45 45 0 0 1 95 50 Z" fill="rgba(91, 192, 190, 0.1)" />
            
            {/* Ownship (Center) */}
            <path d="M50 42 L56 58 L50 54 L44 58 Z" fill="var(--c-phos)" />
            
            {/* Target 1 (Reddish) */}
            <path d="M32 28 L38 38 L32 35 L26 38 Z" fill="#ff4d4d" transform="rotate(-40 32 28)" />
            
            {/* Target 2 (Yellowish) */}
            <path d="M72 45 L78 55 L72 52 L66 55 Z" fill="#ffcc00" transform="rotate(30 72 45)" />
          </svg>
          {/* Subtle Radar Sweep Animation Effect */}
          <div style={{
            position: 'absolute', top: 0, left: 0, right: 0, bottom: 0,
            background: 'conic-gradient(from 0deg, transparent 0deg, rgba(91, 192, 190, 0.05) 90deg, transparent 95deg)',
            animation: 'radar-sweep 4s linear infinite',
            pointerEvents: 'none'
          }} />
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', lineHeight: 1.2 }}>
          <span style={{
            fontFamily: 'var(--f-disp)', fontSize: 18, color: 'var(--c-phos)',
            letterSpacing: '0.04em', fontWeight: 800, textTransform: 'uppercase',
          }}>MASS战术决策系统</span>
          <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em', opacity: 0.8 }}>
            MASS TDL SYSTEM
          </span>
        </div>
      </div>

      {/* Nav Tabs - Centered */}
      <div style={{ 
        position: 'absolute', left: '50%', transform: 'translateX(-50%)', 
        height: '100%', display: 'flex', alignItems: 'stretch' 
      }}>
        {TABS.map((t) => {
          const active = currentScreen === t.id;
          return (
            <div key={t.id} onClick={() => handleNavigate(t.id)} title={t.desc} style={{
              display: 'flex', alignItems: 'center', gap: 12, padding: '0 24px', cursor: 'pointer',
              borderRight: '1px solid var(--line-1)',
              background: active ? 'linear-gradient(180deg, rgba(91,192,190,0.15) 0%, rgba(7,12,19,0) 100%)' : 'transparent',
              borderBottom: active ? '3px solid var(--c-phos)' : '3px solid transparent',
              position: 'relative',
              transition: 'all 0.2s ease',
            }}>
              {active && (
                <div style={{
                  position: 'absolute', top: 0, left: 0, right: 0, height: 1,
                  background: 'var(--c-phos)', opacity: 0.5,
                  boxShadow: '0 0 8px var(--c-phos)'
                }} />
              )}
              <span style={{
                fontFamily: 'var(--f-mono)', fontSize: 14,
                color: active ? 'var(--c-phos)' : 'var(--txt-3)',
                fontWeight: active ? 700 : 500,
              }}>{t.code}</span>
              <div style={{ display: 'flex', flexDirection: 'column', lineHeight: 1.2 }}>
                <span style={{
                  fontFamily: 'var(--f-disp)', fontSize: 14,
                  color: active ? 'var(--txt-0)' : 'var(--txt-2)',
                  letterSpacing: '0.08em', fontWeight: active ? 700 : 500, textTransform: 'uppercase',
                }}>{t.zh}</span>
                <span style={{
                  fontFamily: 'var(--f-body)', fontSize: 10, color: active ? 'var(--txt-1)' : 'var(--txt-3)',
                  marginTop: 2,
                }}>{t.label}</span>
              </div>
            </div>
          );
        })}
      </div>

      <div style={{ flex: 1 }} />

      {/* Right: State Pill + Dual Clock + View Toggle + REC */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '0 12px' }}>
        <RunStatePill state={runState} />
        <DualClock simTime={simTime} showSim={currentScreen === 'bridge'} />

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

        {/* Version */}
        <div style={{
          fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--txt-3)',
          borderLeft: '1px solid var(--line-2)', paddingLeft: 14, marginLeft: 6,
        }}>
          V1.1.3版本
        </div>
      </div>
    </div>
  );
};
