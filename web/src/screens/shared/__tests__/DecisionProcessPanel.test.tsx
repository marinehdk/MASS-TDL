import { describe, expect, it } from 'vitest';
import { render, screen } from '@testing-library/react';
import { DecisionProcessPanel } from '../DecisionProcessPanel';
import type { AvoidancePhaseState } from '../avoidancePhase';
import type { SAT2Data, SotifMetrics } from '../../../types/sat';

const basePhase: AvoidancePhaseState = {
  phase: 'RISK_RULE_ASSESSED',
  phaseLabel: '风险触发与规则判定',
  phaseReason: 'CPA/TCPA 进入危险窗口，M6 生成规则约束',
  activeModules: ['M2', 'M6'],
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
      behavior: '—',
      headingWindow: '—',
      speedWindow: '—',
      confidence: null,
    },
    m5: {
      status: '—',
      waypointCount: 0,
      horizonS: null,
      candidateCount: 0,
      optimalCandidateCost: null,
    },
    m7: {
      severity: '—',
      recommendedMrm: null,
      violatedMetricCount: 0,
      description: '—',
    },
  },
};

const sat2WithChain: SAT2Data = {
  active_behavior: null,
  active_behavior_weight: 0,
  reasoning_latency_ms: 2.3,
  colregs_chain_target_id: '100000001',
  ivp_contributions: [],
  colregs_chain: [
    { layer: 1, label: 'ODD', conclusion: 'ODD-A', inputs: { odd: 'A' } },
    { layer: 2, label: '会遇分类', conclusion: 'Rule 15', inputs: { relative_bearing: '045', range_nm: 1.2 } },
    { layer: 3, label: '责任', conclusion: 'GIVE-WAY', inputs: { role: 'give_way' } },
    { layer: 4, label: '方向', conclusion: 'STARBOARD', inputs: { min_alter_deg: 30 } },
    { layer: 5, label: '时机', conclusion: 'STAGE_2', inputs: { tcpa_min: 6 } },
  ],
};

const nominalSotif: SotifMetrics = {
  ais_radar_consistency_sigma: 1.0,
  target_predictability_rms_m: 10,
  perception_coverage_pct: 95,
  colregs_parse_failures: 0,
  comm_link_rtt_ms: 30,
  checker_veto_rate_pct: 0,
};

describe('DecisionProcessPanel', () => {
  it('renders phase header and M2-M7 chain', () => {
    render(<DecisionProcessPanel phaseState={basePhase} sat2={null} sotifMetrics={null} safetyAlert={null} />);

    expect(screen.getByText('风险触发与规则判定')).toBeInTheDocument();
    expect(screen.getByText('M2 态势')).toBeInTheDocument();
    expect(screen.getByText('M6 规则')).toBeInTheDocument();
    expect(screen.getByText('M4 仲裁')).toBeInTheDocument();
    expect(screen.getByText('M5 规划')).toBeInTheDocument();
    expect(screen.getByText('M7 安全')).toBeInTheDocument();
  });

  it('renders decision values and M2 geometry fallback', () => {
    render(<DecisionProcessPanel phaseState={basePhase} sat2={null} sotifMetrics={null} safetyAlert={null} />);

    expect(screen.getByText('GIVE-WAY')).toBeInTheDocument();
    expect(screen.getByText('STARBOARD')).toBeInTheDocument();
    expect(screen.getByText('相对方位 BRG')).toBeInTheDocument();
    expect(screen.getByText('当前距离 RNG')).toBeInTheDocument();
    expect(screen.getByText('最近会遇 CPA')).toBeInTheDocument();
    expect(screen.getByText('会遇时间 TCPA')).toBeInTheDocument();
    expect(screen.getAllByText('50.0°').length).toBeGreaterThanOrEqual(1);
    expect(screen.getAllByText('1.19 nm').length).toBeGreaterThanOrEqual(1);
    expect(screen.getAllByText('0.65 nm').length).toBeGreaterThanOrEqual(1);
    expect(screen.getAllByText('6.0 min').length).toBeGreaterThanOrEqual(1);
  });

  it('renders missing values as dash instead of fake conclusions', () => {
    const emptyPhase: AvoidancePhaseState = {
      ...basePhase,
      activeModules: ['M2'],
      chain: {
        ...basePhase.chain,
        m2: { targetCount: 0, nearestTargetId: null, rangeNm: null, bearingDeg: null, cpaNm: null, tcpaMin: null, encounter: null },
        m6: { rule: '—', role: '—', preferredDirection: '—', minAlterationDeg: null, phase: '—', rationaleLayerCount: 0 },
      },
    };

    render(<DecisionProcessPanel phaseState={emptyPhase} sat2={null} sotifMetrics={null} safetyAlert={null} />);

    expect(screen.getAllByText('—').length).toBeGreaterThan(5);
  });

  it('renders M6 rationale tree when chain exists and prefers its inputs', () => {
    render(<DecisionProcessPanel phaseState={basePhase} sat2={sat2WithChain} sotifMetrics={null} safetyAlert={null} />);

    expect(screen.getByText('M6 COLREGs REASONING')).toBeInTheDocument();
    expect(screen.getAllByText(/relative_bearing: 045/).length).toBeGreaterThanOrEqual(1);
  });

  it('renders SOTIF monitor strip in M7 section', () => {
    render(<DecisionProcessPanel phaseState={basePhase} sat2={null} sotifMetrics={nominalSotif} safetyAlert={null} />);

    expect(screen.getByTestId('sotif-metrics-panel')).toBeInTheDocument();
    expect(screen.getByText('M7 SOTIF 假设监控')).toBeInTheDocument();
  });

  it('renders top directional costs for M4 arbitration', () => {
    const sat2WithCosts: SAT2Data = {
      ...sat2WithChain,
      ivp_contributions: [
        { direction_deg: 0, cost: 0.2 },
        { direction_deg: 90, cost: 0.9 },
        { direction_deg: 180, cost: 0.4 },
        { direction_deg: 270, cost: 0.7 },
      ],
    };

    render(<DecisionProcessPanel phaseState={basePhase} sat2={sat2WithCosts} sotifMetrics={null} safetyAlert={null} />);

    expect(screen.getByText('方向代价 Top3')).toBeInTheDocument();
    expect(screen.getByText('090° 0.90 · 270° 0.70 · 180° 0.40')).toBeInTheDocument();
  });

  it('ignores malformed live arbitration values instead of crashing', () => {
    const malformedLiveSat2 = {
      ...sat2WithChain,
      reasoning_latency_ms: undefined,
      ivp_contributions: [
        { direction_deg: 90, cost: undefined },
        { direction_deg: '180', cost: 0.8 },
        { direction_deg: 270, cost: 0.7 },
      ],
    } as unknown as SAT2Data;

    render(<DecisionProcessPanel phaseState={basePhase} sat2={malformedLiveSat2} sotifMetrics={null} safetyAlert={null} />);

    expect(screen.getByText('M6 COLREGs REASONING')).toBeInTheDocument();
    expect(screen.getByText('270° 0.70')).toBeInTheDocument();
  });

  it('renders trajectory candidate summary for M5', () => {
    const withCandidates: AvoidancePhaseState = {
      ...basePhase,
      activeModules: ['M2', 'M4', 'M5'],
      chain: {
        ...basePhase.chain,
        m5: {
          status: 'SOLVED',
          waypointCount: 2,
          horizonS: 120,
          candidateCount: 3,
          optimalCandidateCost: 0.18,
        },
      },
    };

    render(<DecisionProcessPanel phaseState={withCandidates} sat2={null} sotifMetrics={null} safetyAlert={null} />);

    expect(screen.getByText('候选轨迹')).toBeInTheDocument();
    expect(screen.getByText('3')).toBeInTheDocument();
    expect(screen.getByText('0.18')).toBeInTheDocument();
  });
});
