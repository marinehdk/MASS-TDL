import { describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { DecisionProcessPanel } from '../DecisionProcessPanel';
import type { AvoidancePhaseState } from '../avoidancePhase';

const basePhase: AvoidancePhaseState = {
  phase: 'CLEAR_RETURN',
  phaseLabel: '解除警报与回归航线',
  phaseReason: '危险解除，M4 回到 Transit，M5 准备回归航线',
  activeModules: ['M2', 'M4', 'M5'],
  events: [],
  chain: {
    m1: { envelope: '—', health: '—', conformance: '—' },
    m2: {
      targetCount: 1,
      nearestTargetId: '100000001',
      rangeNm: 1.19,
      bearingDeg: 50,
      cpaNm: 0.65,
      tcpaMin: 6,
      encounter: 'CROSSING',
    },
    m6: {
      rule: '15',
      role: 'GIVE-WAY',
      preferredDirection: 'STARBOARD',
      minAlterationDeg: 30,
      phase: 'T_avoid',
      rationaleLayerCount: 5,
    },
    m4: {
      behavior: 'TRANSIT',
      headingWindow: '—',
      speedWindow: '—',
      confidence: null,
    },
    m5: {
      status: 'NORMAL',
      waypointCount: 0,
      horizonS: 0,
      candidateCount: 0,
      optimalCandidateCost: null,
    },
    m7: {
      severity: 'STANDBY',
      recommendedMrm: null,
      violatedMetricCount: 0,
      description: '—',
    },
  },
};

describe('DecisionProcessPanel', () => {
  it('renders current phase overview and five-stage flow', () => {
    render(<DecisionProcessPanel phaseState={basePhase} sat2={null} sotifMetrics={null} safetyAlert={null} />);

    expect(screen.getByTestId('decision-process-panel')).toBeInTheDocument();
    expect(screen.queryByText('避碰过程')).not.toBeInTheDocument();
    expect(screen.getByText('当前阶段')).toBeInTheDocument();
    expect(screen.getByText('解除警报与回归航线')).toBeInTheDocument();
    expect(screen.getAllByText('危险解除，规则清空，系统回到 Transit 并准备回归航线。').length).toBeGreaterThanOrEqual(1);
    expect(screen.queryByText('底部 M4/M5 查看回归行为与规划状态。')).not.toBeInTheDocument();
    expect(screen.getByTestId('decision-phase-TRANSIT_DISCOVERY')).toBeInTheDocument();
    expect(screen.getByTestId('decision-phase-RISK_RULE_ASSESSED')).toBeInTheDocument();
    expect(screen.getByTestId('decision-phase-ARBITRATION_MANEUVERING')).toBeInTheDocument();
    expect(screen.getByTestId('decision-phase-SAFETY_MONITORING')).toBeInTheDocument();
    expect(screen.getByTestId('decision-phase-CLEAR_RETURN')).toBeInTheDocument();
  });

  it('does not render stale module chain details', () => {
    render(<DecisionProcessPanel phaseState={basePhase} sat2={null} sotifMetrics={null} safetyAlert={null} />);

    expect(screen.queryByText('M2 态势')).not.toBeInTheDocument();
    expect(screen.queryByText('M5 规划')).not.toBeInTheDocument();
    expect(screen.queryByText('M7 安全')).not.toBeInTheDocument();
    expect(screen.queryByText('M6 COLREGs REASONING')).not.toBeInTheDocument();
    expect(screen.queryByText('方向代价 Top3')).not.toBeInTheDocument();
    expect(screen.queryByText('SOTIF 风险与系统警报')).not.toBeInTheDocument();
  });

  it('renders latest event summary when available', () => {
    render(
      <DecisionProcessPanel
        phaseState={{
          ...basePhase,
          events: [{ t: 12, k: 'CLEAR_RETURN', sev: 'info', m: 'M8', d: '解除警报，回归航线' }],
        }}
        sat2={null}
        sotifMetrics={null}
        safetyAlert={null}
      />,
    );

    expect(screen.getByText('最近事件：M8 解除警报，回归航线')).toBeInTheDocument();
  });

  it('selects bottom module from compact module chips', () => {
    const onModuleSelect = vi.fn();

    render(<DecisionProcessPanel phaseState={basePhase} sat2={null} sotifMetrics={null} safetyAlert={null} onModuleSelect={onModuleSelect} />);

    fireEvent.click(screen.getAllByRole('button', { name: 'M4' })[0]);

    expect(onModuleSelect).toHaveBeenCalledWith('M4');
  });
});
