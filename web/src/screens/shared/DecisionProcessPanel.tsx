import React from 'react';
import type { SAT2Data, SotifMetrics } from '../../types/sat';
import type { AvoidancePhase, AvoidancePhaseState, DecisionModule } from './avoidancePhase';

interface DecisionProcessPanelProps {
  phaseState: AvoidancePhaseState;
  sat2: SAT2Data | null;
  sotifMetrics: SotifMetrics | null;
  safetyAlert: { recommendedMrm?: string | null } | null;
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
  detailHint: string;
}> = [
  {
    id: 'TRANSIT_DISCOVERY',
    label: '目标发现',
    modules: ['M2'],
    summary: 'M2 持续监控目标，CPA/TCPA 尚未触发规则约束。',
    detailHint: '底部 M2 查看目标与最近会遇指标。',
  },
  {
    id: 'RISK_RULE_ASSESSED',
    label: '规则判定',
    modules: ['M2', 'M6'],
    summary: '风险进入阈值，M6 输出 COLREGs 规则、责任角色与首选方向。',
    detailHint: '底部 M6 查看 Rule / Role / Direction。',
  },
  {
    id: 'ARBITRATION_MANEUVERING',
    label: '仲裁避让',
    modules: ['M4', 'M5'],
    summary: 'M4 选择避让行为，M5 生成轨迹或速度/转向指令。',
    detailHint: '底部 M4/M5 查看行为、窗口、路径点与指令。',
  },
  {
    id: 'SAFETY_MONITORING',
    label: '安全监督',
    modules: ['M7'],
    summary: 'M7 独立检查风险；必要时 veto 当前动作或触发 MRM。',
    detailHint: '底部 M7/M8 查看安全告警、否决率与报警状态。',
  },
  {
    id: 'CLEAR_RETURN',
    label: '解除回归',
    modules: ['M4', 'M5', 'M8'],
    summary: '危险解除，规则清空，系统回到 Transit 并准备回归航线。',
    detailHint: '底部 M4/M5 查看回归行为与规划状态。',
  },
];

function ModuleChip({ module }: { module: DecisionModule }) {
  const color = MODULE_COLORS[module];
  return (
    <span
      style={{
        color,
        border: `1px solid ${color}55`,
        background: `${color}18`,
        borderRadius: 4,
        padding: '2px 5px',
        fontFamily: 'var(--f-mono)',
        fontSize: 9,
        fontWeight: 800,
      }}
    >
      {module}
    </span>
  );
}

export const DecisionProcessPanel: React.FC<DecisionProcessPanelProps> = ({ phaseState }) => {
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

      <section style={{ padding: '12px 14px', borderBottom: '1px solid var(--line-2)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 8 }}>
          <span style={{ width: 6, height: 16, borderRadius: 2, background: 'var(--c-phos)' }} />
          <span style={{ color: 'var(--c-phos)', fontFamily: 'var(--f-disp)', fontSize: 12, fontWeight: 900, letterSpacing: '0.08em' }}>
            当前阶段概述
          </span>
        </div>
        <div style={{ color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', fontSize: 12, lineHeight: 1.55 }}>
          {activePhase.summary}
        </div>
        <div style={{ marginTop: 10, display: 'flex', gap: 6, flexWrap: 'wrap' }}>
          {activePhase.modules.map((module) => <ModuleChip key={module} module={module} />)}
        </div>
        <div style={{ marginTop: 9, color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10 }}>
          {activePhase.detailHint}
        </div>
      </section>

      <section style={{ padding: '12px 14px' }}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
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
                  <div style={{ display: 'flex', justifyContent: 'space-between', gap: 8 }}>
                    <span style={{ color, fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 800, letterSpacing: '0.06em' }}>
                      {String(index + 1).padStart(2, '0')} {phase.label}
                    </span>
                    <span style={{ color, fontFamily: 'var(--f-mono)', fontSize: 9 }}>
                      {phase.modules.join('/')}
                    </span>
                  </div>
                  {isActive ? (
                    <div style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 9, marginTop: 3, lineHeight: 1.45 }}>
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
