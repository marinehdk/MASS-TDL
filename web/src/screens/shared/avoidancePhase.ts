import type { SAT2Data, SAT3Data, SotifMetrics } from '../../types/sat';

export type AvoidancePhase =
  | 'TRANSIT_DISCOVERY'
  | 'RISK_RULE_ASSESSED'
  | 'ARBITRATION_MANEUVERING'
  | 'SAFETY_MONITORING'
  | 'CLEAR_RETURN';

export type DecisionModule = 'M1' | 'M2' | 'M3' | 'M4' | 'M5' | 'M6' | 'M7' | 'M8';

export interface DecisionTimelineEvent {
  t: number;
  k: string;
  sev: 'info' | 'warn' | 'crit';
  m: 'M2' | 'M4' | 'M5' | 'M6' | 'M7' | 'M8';
  d: string;
}

export interface DecisionChainSnapshot {
  m1: {
    envelope: string;
    health: string;
    conformance: string;
  };
  m2: {
    targetCount: number;
    nearestTargetId: string | null;
    rangeNm: number | null;
    bearingDeg: number | null;
    cpaNm: number | null;
    tcpaMin: number | null;
    encounter: string | null;
  };
  m6: {
    rule: string;
    role: string;
    preferredDirection: string;
    minAlterationDeg: number | null;
    phase: string;
    rationaleLayerCount: number;
  };
  m4: {
    behavior: string;
    headingWindow: string;
    speedWindow: string;
    confidence: number | null;
  };
  m5: {
    status: string;
    waypointCount: number;
    horizonS: number | null;
    candidateCount: number;
    optimalCandidateCost: number | null;
  };
  m7: {
    severity: string;
    recommendedMrm: string | null;
    violatedMetricCount: number;
    description: string;
  };
}

export interface AvoidancePhaseInput {
  simTimeSec: number;
  targets: Array<{
    mmsi?: number | string;
    cpaM?: number;
    tcpaS?: number;
    rngM?: number;
    brgDeg?: number;
    encounter?: string;
  }>;
  oddState: {
    envelopeState?: number | string | null;
    health?: number | string | null;
    conformanceScore?: number | null;
  } | null;
  colregsConstraint: {
    ruleId?: string | number | null;
    role?: string | number | null;
    preferredDirection?: string | number | null;
    minAlterationDeg?: number | null;
    phase?: string | null;
  } | null;
  behaviorPlan: {
    behavior?: string | number | null;
    headingMinDeg?: number | null;
    headingMaxDeg?: number | null;
    speedMinKn?: number | null;
    speedMaxKn?: number | null;
    confidence?: number | null;
  } | null;
  avoidancePlan: {
    status?: string | null;
    horizonS?: number | null;
    confidence?: number | null;
    waypoints?: unknown[];
  } | null;
  safetyAlert: {
    severity?: string | number | null;
    recommendedMrm?: string | null;
    description?: string | null;
    confidence?: number | null;
  } | null;
  sat2: SAT2Data | null;
  sat3: SAT3Data | null;
  sotifMetrics: SotifMetrics | null;
  previousPhase?: AvoidancePhase | null;
}

export interface AvoidancePhaseState {
  phase: AvoidancePhase;
  phaseLabel: string;
  phaseReason: string;
  chain: DecisionChainSnapshot;
  events: DecisionTimelineEvent[];
  activeModules: DecisionModule[];
}

export const PHASE_LABELS: Record<AvoidancePhase, string> = {
  TRANSIT_DISCOVERY: '自由航行与目标发现',
  RISK_RULE_ASSESSED: '风险触发与规则判定',
  ARBITRATION_MANEUVERING: '行为仲裁与轨迹生成',
  SAFETY_MONITORING: '安全监督与持续避让',
  CLEAR_RETURN: '解除警报与回归航线',
};

function nmFromM(value: number | undefined): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value / 1852.0 : null;
}

function minFromS(value: number | undefined): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value / 60.0 : null;
}

function asText(value: unknown, fallback = '—'): string {
  if (value === null || value === undefined || value === '') return fallback;
  return String(value);
}

function formatWindow(min?: number | null, max?: number | null, unit = ''): string {
  if (typeof min !== 'number' || typeof max !== 'number') return '—';
  return `${min.toFixed(1)}-${max.toFixed(1)}${unit}`;
}

function formatPercent(value?: number | null): string {
  if (typeof value !== 'number' || !Number.isFinite(value)) return '—';
  return `${(value * 100).toFixed(0)}%`;
}

function isNoneRule(ruleId: unknown): boolean {
  if (ruleId === null || ruleId === undefined || ruleId === '') return true;
  const text = String(ruleId).toUpperCase();
  return text === '0' || text === 'NONE' || text === 'NULL';
}

function isTransitBehavior(behavior: unknown): boolean {
  if (behavior === null || behavior === undefined || behavior === '') return true;
  const text = String(behavior).toUpperCase();
  return text === '0' || text.includes('TRANSIT') || text.includes('NORMAL') || text.includes('TRACK');
}

function isSafetySeverityActive(severity: unknown): boolean {
  if (severity === null || severity === undefined || severity === '') return false;
  if (typeof severity === 'number') return severity > 0;
  const text = String(severity).toUpperCase();
  return !['0', 'NONE', 'NOMINAL', 'CLEAR', 'OK'].includes(text);
}

function countSotifViolations(metrics: SotifMetrics | null): number {
  if (!metrics) return 0;
  return [
    metrics.ais_radar_consistency_sigma > 2.0,
    metrics.target_predictability_rms_m > 50,
    metrics.perception_coverage_pct < 80,
    metrics.colregs_parse_failures > 3,
    metrics.comm_link_rtt_ms > 2000,
    metrics.checker_veto_rate_pct > 20,
  ].filter(Boolean).length;
}

function findNearestTarget(input: AvoidancePhaseInput) {
  const enriched = input.targets.map((target) => {
    const cpaNm = nmFromM(target.cpaM);
    const tcpaMin = minFromS(target.tcpaS);
    const rangeNm = nmFromM(target.rngM);
    return { target, cpaNm, tcpaMin, rangeNm };
  });

  return enriched.sort((a, b) => {
    const aScore = a.cpaNm ?? a.rangeNm ?? Number.POSITIVE_INFINITY;
    const bScore = b.cpaNm ?? b.rangeNm ?? Number.POSITIVE_INFINITY;
    return aScore - bScore;
  })[0] ?? null;
}

function hasDangerousCpa(cpaNm: number | null, tcpaMin: number | null): boolean {
  if (cpaNm === null || tcpaMin === null) return false;
  return cpaNm < 1.0 && tcpaMin >= 0 && tcpaMin <= 15;
}

function hasCriticalCpa(cpaNm: number | null, tcpaMin: number | null): boolean {
  if (cpaNm === null || tcpaMin === null) return false;
  return cpaNm < 0.5 && tcpaMin >= 0 && tcpaMin <= 10;
}

function riskyHistory(phase?: AvoidancePhase | null): boolean {
  return phase === 'RISK_RULE_ASSESSED'
    || phase === 'ARBITRATION_MANEUVERING'
    || phase === 'SAFETY_MONITORING';
}

function bestCandidateCost(input: AvoidancePhaseInput): number | null {
  const candidates = input.sat3?.trajectory_candidates ?? [];
  const optimal = candidates.find((candidate) => candidate.is_optimal) ?? candidates[0];
  return typeof optimal?.cost === 'number' ? optimal.cost : null;
}

export function deriveAvoidancePhaseState(input: AvoidancePhaseInput): AvoidancePhaseState {
  const nearest = findNearestTarget(input);
  const cpaNm = nearest?.cpaNm ?? null;
  const tcpaMin = nearest?.tcpaMin ?? null;
  const hasRule = !isNoneRule(input.colregsConstraint?.ruleId)
    || Boolean(input.sat2?.colregs_chain?.length);
  const hasManeuver = !isTransitBehavior(input.behaviorPlan?.behavior)
    || Boolean(input.sat2?.active_behavior && !isTransitBehavior(input.sat2.active_behavior))
    || Boolean(input.avoidancePlan?.waypoints?.length)
    || Boolean(input.sat3?.trajectory_candidates?.length)
    || Boolean(input.avoidancePlan?.status && !['IDLE', 'NONE', 'CLEAR'].includes(input.avoidancePlan.status.toUpperCase()));
  const violatedMetricCount = countSotifViolations(input.sotifMetrics);
  const hasSafety = isSafetySeverityActive(input.safetyAlert?.severity)
    || Boolean(input.safetyAlert?.recommendedMrm)
    || violatedMetricCount > 0;
  const dangerous = hasDangerousCpa(cpaNm, tcpaMin);
  const critical = hasCriticalCpa(cpaNm, tcpaMin);
  const clearReturn = riskyHistory(input.previousPhase)
    && !hasRule
    && !dangerous
    && !hasSafety
    && isTransitBehavior(input.behaviorPlan?.behavior);

  let phase: AvoidancePhase = 'TRANSIT_DISCOVERY';
  let phaseReason = 'M2 持续监控目标，尚未形成规则约束';

  if (clearReturn) {
    phase = 'CLEAR_RETURN';
    phaseReason = '危险解除，M4 回到 Transit，M5 准备回归航线';
  } else if (hasSafety) {
    phase = 'SAFETY_MONITORING';
    phaseReason = 'M7 Checker 或 SOTIF 指标触发安全监督';
  } else if (hasManeuver) {
    phase = 'ARBITRATION_MANEUVERING';
    phaseReason = 'M4/M5 已进入避让仲裁或轨迹生成';
  } else if (hasRule || dangerous) {
    phase = 'RISK_RULE_ASSESSED';
    phaseReason = 'CPA/TCPA 进入危险窗口，M6 生成规则约束';
  }

  const events: DecisionTimelineEvent[] = [];
  if (phase === 'RISK_RULE_ASSESSED') {
    events.push({
      t: input.simTimeSec,
      k: 'M6_RULE_ASSERTED',
      sev: critical ? 'crit' : 'warn',
      m: 'M6',
      d: hasRule ? `规则 ${asText(input.colregsConstraint?.ruleId, 'COLREGs')}` : 'CPA/TCPA 进入危险窗口',
    });
  }
  if (phase === 'ARBITRATION_MANEUVERING') {
    events.push(
      { t: input.simTimeSec, k: 'M4_MANEUVER_START', sev: 'warn', m: 'M4', d: `行为 ${asText(input.behaviorPlan?.behavior ?? input.sat2?.active_behavior)}` },
      { t: input.simTimeSec, k: 'M5_PLAN_READY', sev: 'info', m: 'M5', d: `${input.avoidancePlan?.waypoints?.length ?? 0} 个避让路点` },
    );
  }
  if (phase === 'SAFETY_MONITORING') {
    events.push({
      t: input.simTimeSec,
      k: 'M7_SAFETY_ALERT',
      sev: 'crit',
      m: 'M7',
      d: input.safetyAlert?.description ?? input.safetyAlert?.recommendedMrm ?? 'SOTIF 指标越阈',
    });
  }
  if (phase === 'CLEAR_RETURN') {
    events.push({ t: input.simTimeSec, k: 'CLEAR_RETURN', sev: 'info', m: 'M8', d: '解除警报，回归航线' });
  }

  const activeModules: DecisionModule[] = ['M2'];
  if (hasRule || dangerous) activeModules.push('M6');
  if (hasManeuver) activeModules.push('M4', 'M5');
  if (hasSafety) activeModules.push('M7');
  if (phase === 'CLEAR_RETURN') activeModules.push('M4', 'M5', 'M8');

  return {
    phase,
    phaseLabel: PHASE_LABELS[phase],
    phaseReason,
    activeModules: Array.from(new Set(activeModules)),
    events,
    chain: {
      m1: {
        envelope: asText(input.oddState?.envelopeState),
        health: asText(input.oddState?.health),
        conformance: formatPercent(input.oddState?.conformanceScore),
      },
      m2: {
        targetCount: input.targets.length,
        nearestTargetId: nearest?.target.mmsi !== undefined ? String(nearest.target.mmsi) : null,
        rangeNm: nearest?.rangeNm ?? null,
        bearingDeg: typeof nearest?.target.brgDeg === 'number' ? nearest.target.brgDeg : null,
        cpaNm,
        tcpaMin,
        encounter: nearest?.target.encounter ?? null,
      },
      m6: {
        rule: asText(input.colregsConstraint?.ruleId),
        role: asText(input.colregsConstraint?.role),
        preferredDirection: asText(input.colregsConstraint?.preferredDirection),
        minAlterationDeg: typeof input.colregsConstraint?.minAlterationDeg === 'number' ? input.colregsConstraint.minAlterationDeg : null,
        phase: asText(input.colregsConstraint?.phase),
        rationaleLayerCount: input.sat2?.colregs_chain?.length ?? 0,
      },
      m4: {
        behavior: asText(input.behaviorPlan?.behavior ?? input.sat2?.active_behavior),
        headingWindow: formatWindow(input.behaviorPlan?.headingMinDeg, input.behaviorPlan?.headingMaxDeg, '°'),
        speedWindow: formatWindow(input.behaviorPlan?.speedMinKn, input.behaviorPlan?.speedMaxKn, ' kn'),
        confidence: typeof input.behaviorPlan?.confidence === 'number' ? input.behaviorPlan.confidence : null,
      },
      m5: {
        status: asText(input.avoidancePlan?.status),
        waypointCount: input.avoidancePlan?.waypoints?.length ?? 0,
        horizonS: typeof input.avoidancePlan?.horizonS === 'number' ? input.avoidancePlan.horizonS : null,
        candidateCount: input.sat3?.trajectory_candidates?.length ?? 0,
        optimalCandidateCost: bestCandidateCost(input),
      },
      m7: {
        severity: asText(input.safetyAlert?.severity),
        recommendedMrm: input.safetyAlert?.recommendedMrm ?? null,
        violatedMetricCount,
        description: input.safetyAlert?.description ?? '—',
      },
    },
  };
}
