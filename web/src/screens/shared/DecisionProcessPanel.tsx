import React from 'react';
import type { SAT2Data, SotifMetrics } from '../../types/sat';
import type { AvoidancePhase, AvoidancePhaseState, DecisionModule } from './avoidancePhase';

interface DecisionProcessPanelProps {
  phaseState: AvoidancePhaseState;
  sat2: SAT2Data | null;
  sotifMetrics: SotifMetrics | null;
  safetyAlert: { recommendedMrm?: string | null } | null;
  onModuleSelect?: (module: DecisionModule) => void;
}

const MODULE_COLORS: Record<DecisionModule, string> = {
  M1: '#5bc0be',
  M2: '#5bc0be',
  M3: '#5bc0be',
  M4: '#60a5fa',
  M5: '#34d399',
  M6: '#fbbf24',
  M7: '#f87171',
  M8: '#f87171',
};

const PHASE_FLOW: Array<{
  id: AvoidancePhase;
  label: string;
  modules: DecisionModule[];
  summary: string;
}> = [
  {
    id: 'TRANSIT_DISCOVERY',
    label: '自由航行与目标发现',
    modules: ['M2'],
    summary: 'M2 持续监控目标，CPA/TCPA 尚未触发规则约束。',
  },
  {
    id: 'RISK_RULE_ASSESSED',
    label: '风险触发与规则判定',
    modules: ['M2', 'M6'],
    summary: '风险进入阈值，M6 输出 COLREGs 规则、责任角色与首选方向。',
  },
  {
    id: 'ARBITRATION_MANEUVERING',
    label: '行为仲裁与轨迹生成',
    modules: ['M4', 'M5'],
    summary: 'M4 选择避让行为，M5 生成轨迹或速度/转向指令。',
  },
  {
    id: 'SAFETY_MONITORING',
    label: '安全监督与持续避让',
    modules: ['M7'],
    summary: 'M7 独立检查风险；必要时 veto 当前动作或触发 MRM。',
  },
  {
    id: 'CLEAR_RETURN',
    label: '解除警报与回归航线',
    modules: ['M4', 'M5', 'M8'],
    summary: '危险解除，规则清空，系统回到 Transit 并准备回归航线。',
  },
];

function ModuleChip({ module, onSelect }: { module: DecisionModule; onSelect?: (module: DecisionModule) => void }) {
  const color = MODULE_COLORS[module];
  const clickable = Boolean(onSelect);
  return (
    <button
      type="button"
      onClick={() => onSelect?.(module)}
      title={`查看底部 ${module} 详情`}
      style={{
        color,
        border: `1px solid ${color}55`,
        background: `${color}18`,
        borderRadius: 4,
        padding: '1px 5px',
        fontFamily: 'var(--f-mono)',
        fontSize: 9,
        fontWeight: 800,
        lineHeight: 1.35,
        cursor: clickable ? 'pointer' : 'default',
      }}
    >
      {module}
    </button>
  );
}

export const DecisionProcessPanel: React.FC<DecisionProcessPanelProps> = ({ phaseState, onModuleSelect }) => {
  const activePhaseIndex = PHASE_FLOW.findIndex((phase) => phase.id === phaseState.phase);
  const activePhase = PHASE_FLOW[activePhaseIndex] ?? PHASE_FLOW[0];
  const latestEvent = phaseState.events[phaseState.events.length - 1];

  return (
    <div
      data-testid="decision-process-panel"
      style={{
        display: 'flex',
        flexDirection: 'column',
        background: 'rgba(10, 15, 24, 0.96)',
        border: '1px solid var(--line-2)',
        borderRadius: 8,
        overflow: 'hidden',
        color: 'var(--txt-1)',
        width: '100%',
      }}
    >
      <section style={{ padding: '12px 14px 10px', borderBottom: '1px solid var(--line-2)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 7 }}>
          <span style={{ width: 6, height: 16, borderRadius: 2, background: 'var(--c-phos)' }} />
          <div style={{ minWidth: 0, display: 'flex', alignItems: 'baseline', gap: 8, flexWrap: 'wrap' }}>
            <span style={{ color: 'var(--c-phos)', fontFamily: 'var(--f-disp)', fontSize: 12, fontWeight: 900, letterSpacing: '0.08em' }}>
              当前阶段
            </span>
            <span data-testid="decision-process-phase" style={{ color: 'var(--txt-1)', fontFamily: 'var(--f-disp)', fontSize: 16, fontWeight: 900, lineHeight: 1.2 }}>
              {phaseState.phaseLabel}
            </span>
          </div>
        </div>
        <div style={{ color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', fontSize: 12, lineHeight: 1.55 }}>
          {activePhase.summary}
        </div>
      </section>

      <section style={{ padding: '12px 14px' }}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 9 }}>
          {PHASE_FLOW.map((phase, index) => {
            const isActive = phase.id === phaseState.phase;
            const isDone = activePhaseIndex > index;
            const color = isActive ? 'var(--c-phos)' : isDone ? 'var(--txt-2)' : 'var(--txt-3)';
            return (
              <div
                key={phase.id}
                data-testid={`decision-phase-${phase.id}`}
                style={{
                  display: 'grid',
                  gridTemplateColumns: '18px 1fr',
                  gap: 8,
                  alignItems: 'start',
                }}
              >
                <div style={{
                  width: 10,
                  height: 10,
                  marginTop: 3,
                  borderRadius: '50%',
                  background: isActive ? 'var(--c-phos)' : isDone ? 'var(--txt-2)' : 'transparent',
                  border: `1px solid ${color}`,
                  boxShadow: isActive ? '0 0 12px rgba(91,192,190,0.8)' : 'none',
                }} />
                <div>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', gap: 8 }}>
                    <span style={{ color, fontFamily: 'var(--f-disp)', fontSize: 15, fontWeight: 900, letterSpacing: '0.01em', lineHeight: 1.2 }}>
                      {String(index + 1).padStart(2, '0')} {phase.label}
                    </span>
                    <span style={{ display: 'flex', gap: 4, flexWrap: 'wrap', justifyContent: 'flex-end' }}>
                      {phase.modules.map((module) => (
                        <ModuleChip key={`${phase.id}-${module}`} module={module} onSelect={onModuleSelect} />
                      ))}
                    </span>
                  </div>
                  {isActive ? (
                    <div style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10, marginTop: 4, lineHeight: 1.45 }}>
                      {phase.summary}
                    </div>
                  ) : null}
                </div>
              </div>
            );
          })}
        </div>

        <div style={{
          marginTop: 12,
          borderTop: '1px solid var(--line-2)',
          paddingTop: 10,
          color: 'var(--txt-3)',
          fontFamily: 'var(--f-mono)',
          fontSize: 10,
          lineHeight: 1.6,
        }}>
          最近事件：{latestEvent ? `${latestEvent.m} ${latestEvent.d}` : '暂无阶段事件'}
        </div>
      </section>
    </div>
  );
};
