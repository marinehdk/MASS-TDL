import { describe, expect, it } from 'vitest';
import { render, screen } from '@testing-library/react';
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
  it('keeps only the cleared shell for the next process mapping', () => {
    render(<DecisionProcessPanel phaseState={basePhase} sat2={null} sotifMetrics={null} safetyAlert={null} />);

    expect(screen.getByTestId('decision-process-panel')).toBeInTheDocument();
    expect(screen.getByText('避碰过程')).toBeInTheDocument();
    expect(screen.getByText('解除警报与回归航线')).toBeInTheDocument();
    expect(screen.getByText('仿真过程链路待重新映射')).toBeInTheDocument();
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
});
