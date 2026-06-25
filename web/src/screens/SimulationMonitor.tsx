import { useRef, useEffect, useState, memo, useMemo } from 'react';
import { SilMapView } from '../map/SilMapView';
import { SafetyDomainLayer } from '../map/SafetyDomainLayer';
import { IvpRiskGradientLayer } from '../map/IvpRiskGradientLayer';
import { MpcTrajectoryLayer } from '../map/MpcTrajectoryLayer';
import { useFoxgloveLive } from '../hooks/useFoxgloveLive';
import { useTelemetryStore, useControlStore, useUIStore, useScenarioStore } from '../store';
import type { ThreatRiskHistorySample, ThreatRiskTargetData, VoyagePlanData } from '../store/telemetryStore';
import {
  useDeactivateLifecycleMutation,
  useChangeLifecycleRateMutation,
  useGetLifecycleStatusQuery,
  useGetScenarioQuery,
} from '../api/silApi';
import * as jsyaml from 'js-yaml';
import { computeRangeNm, computeBearing, computeCpaTcpa } from './shared/navMath';
import { RadarPpiDisplay } from '../map/RadarPpiDisplay';
import { DistanceScale } from '../map/DistanceScale';
import { MapLayerSwitcher } from '../map/MapLayerSwitcher';
import { ArpaTargetTable } from './shared/ArpaTargetTable';
import { ScoringGauges } from './shared/ScoringGauges';
import { TorModal } from './shared/TorModal';
import { FaultInjectPanel } from './shared/FaultInjectPanel';
import { PlannedRouteLayer } from '../map/PlannedRouteLayer';
import { ActualTrackLayer } from '../map/ActualTrackLayer';
import { AvoidanceRouteLayer } from '../map/AvoidanceRouteLayer';
import { EncounterInjectPanel } from './shared/EncounterInjectPanel';
import { ColregsRationaleTree } from './shared/ColregsRationaleTree';
import { DecisionChainTimingBar } from './shared/DecisionChainTimingBar';
import { SotifMonitorStrip } from './shared/SotifMonitorStrip';
import { DecisionProcessPanel } from './shared/DecisionProcessPanel';
import { deriveAvoidancePhaseState, type AvoidancePhase } from './shared/avoidancePhase';
import { DecisionEventMarkers } from './shared/DecisionEventMarkers';
import { useFsmStore } from '../store';
import { useHotkeys } from '../hooks/useHotkeys';
import { FsmStatePanel } from '../components/FsmStatePanel';
import {
  LucidePlay, LucidePause, LucideSquare,
  LucideTerminalSquare, LucideAlertTriangle, LucidePanelLeft, LucidePanelRight,
  LucideCompass, LucideActivity, LucideAward, LucideZap, LucideChevronRight,
  LucideNavigation,
} from 'lucide-react';
import type maplibregl from 'maplibre-gl';

const MODULE_NAMES = ['M1', 'M2', 'M3', 'M4', 'M5', 'M6', 'M7', 'M8'] as const;
type ModuleName = typeof MODULE_NAMES[number];
type ModuleDetailRow = { label: string; value: string };
const HEALTH_COLOR: Record<number, string> = { 1: '#34d399', 2: '#fbbf24', 3: '#f87171' };

function fmtSimTime(secs: number) {
  const m = Math.floor(secs / 60).toString().padStart(2, '0');
  const s = Math.floor(secs % 60).toString().padStart(2, '0');
  return `${m}:${s}`;
}

const FSM_BORDER: Record<string, string> = {
  TRANSIT:           'transparent',
  COLREG_AVOIDANCE:  'transparent',
  TOR:               'var(--c-warn)',
  OVERRIDE:          'transparent',
  MRC:               '#8b0000',
};
const FSM_GLOW: Record<string, string> = {
  TOR: '0 0 0 3px rgba(251,191,36,0.4)',
  MRC: '0 0 0 3px rgba(139,0,0,0.6)',
};

const ModulePulseBar = memo(function ModulePulseBar() {
  const pulses = useTelemetryStore((s) => s.modulePulses);
  const byId: Record<number, typeof pulses[0]> = {};
  for (const p of pulses) { if (p.moduleId != null) byId[Number(p.moduleId)] = p; }

  return (
    <div style={{
      display: 'flex', gap: 2, alignItems: 'center', height: 16, background: '#070C13',
      position: 'absolute', top: 0, left: 0, right: 0, zIndex: 30, justifyContent: 'center',
    }}>
      {MODULE_NAMES.map((name, i) => {
        const p = byId[i + 1];
        const color = p ? (HEALTH_COLOR[p.state ?? 0] ?? '#444') : '#333';
        const lat = p?.latencyMs;
        const tip = p
          ? `${name}: ${p.state === 1 ? 'GREEN' : p.state === 2 ? 'AMBER' : 'RED'} | ${lat ?? '?'}ms`
          : `${name}: no data`;
        return (
          <div key={name} title={tip} style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '0 8px', borderRight: '1px solid #1B2C44' }}>
            <div style={{ width: 6, height: 6, borderRadius: '50%', background: color }} />
            <span style={{ color: '#8A9AAD', fontSize: 9, fontFamily: 'var(--f-mono)' }}>{name}</span>
          </div>
        );
      })}
    </div>
  );
});

function LeftDrawer() {
  const sat2 = useTelemetryStore((s) => s.sat2);
  const targets = useTelemetryStore((s) => s.targets);
  const fsmState = useFsmStore((s) => s.currentState);
  const fsmRule = useFsmStore((s) => s.activeRule);
  const fsmConf = useFsmStore((s) => s.confidence);
  const fsmHistory = useFsmStore((s) => s.transitionHistory);
  const [fsmExpanded, setFsmExpanded] = useState(true);

  return (
    <div style={{
      position: 'absolute', top: 16, left: 0, bottom: 80, width: 300,
      background: 'rgba(7,12,19,0.92)', backdropFilter: 'blur(8px)',
      borderRight: '1px solid var(--line-2)', zIndex: 20, overflowY: 'auto',
    }}>
      <FsmStatePanel
        state={fsmState}
        activeRule={fsmRule}
        confidence={fsmConf}
        history={fsmHistory}
        expanded={fsmExpanded}
        onToggleExpand={() => setFsmExpanded(!fsmExpanded)}
      />

      <div style={{ borderBottom: '1px solid var(--line-2)', padding: '6px 12px' }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
          ① ARPA 目标表
        </span>
      </div>
      <div style={{ maxHeight: 180, overflowY: 'auto' }}>
        <ArpaTargetTable targets={targets} compact />
      </div>

      <div style={{ borderBottom: '1px solid var(--line-2)', borderTop: '1px solid var(--line-2)', padding: '6px 12px', marginTop: 4 }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
          ② M6 决策溯源
        </span>
      </div>
      <ColregsRationaleTree
        chain={sat2?.colregs_chain ?? []}
        targetId={sat2?.colregs_chain_target_id ?? null}
        latencyMs={sat2?.reasoning_latency_ms ?? 0}
      />
    </div>
  );
}

const MONITOR_TABS = [
  { id: 'asdr',       label: 'ASDR 记录账本',     icon: <LucideTerminalSquare size={20} /> },
  { id: 'score',      label: '五维实时评分',      icon: <LucideAward size={20} /> },
  { id: 'fault',      label: '故障测试注入',      icon: <LucideZap size={20} /> },
  { id: 'encounter',  label: '遭遇注入',          icon: <LucideAlertTriangle size={20} /> },
] as const;

const CAPTAIN_TABS = [
  { id: 'ship',   label: '本船状态', icon: <LucideCompass size={20} /> },
  { id: 'threat', label: '威胁列表', icon: <LucideAlertTriangle size={20} /> },
  { id: 'avoid',  label: '避碰决策', icon: <LucideNavigation size={20} /> },
] as const;

const RIGHT_TABS = [...CAPTAIN_TABS, ...MONITOR_TABS] as const;
const RADAR_RANGES_NM = [10, 6, 2] as const;

type MonitorTabId = typeof MONITOR_TABS[number]['id'];
type CaptainTabId = typeof CAPTAIN_TABS[number]['id'];
type RightTabId = MonitorTabId | CaptainTabId;
type RadarRangeNm = typeof RADAR_RANGES_NM[number];

interface SimulationMonitorProps {
  routeScenarioId?: string;
}

interface RouteProgress {
  nextWaypointIndex: number;
  remainingDistanceNm: number;
  remainingTimeS: number;
  plannedSpeedKn: number;
}

function finiteNumber(value: unknown): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function segmentSpeedKn(speedProfile: number[] | undefined, index: number, fallback: number): number {
  const profiled = finiteNumber(speedProfile?.[index]);
  if (profiled !== null && profiled > 0) return profiled;
  return fallback > 0 ? fallback : 10.0;
}

function formatRemainingSimTime(seconds: number | null): string {
  if (seconds === null || seconds < 0 || !Number.isFinite(seconds)) return '—';
  if (seconds < 60) return `${Math.round(seconds)} s`;
  if (seconds < 3600) return `${(seconds / 60).toFixed(1)} min`;
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.round((seconds - hours * 3600) / 60);
  if (minutes >= 60) return `${hours + 1}h 0m`;
  return `${hours}h ${minutes}m`;
}

const ODD_ENVELOPE_LABELS: Record<number, string> = {
  0: 'IN ODD',
  1: 'EDGE',
  2: 'OUT',
  3: 'MRC PREP',
  4: 'MRC ACTIVE',
};

const COLREGS_ROLE_LABELS: Record<number, string> = {
  0: 'STAND-ON 保向',
  1: 'GIVE-WAY 让路',
  2: 'BOTH GIVE-WAY',
  3: 'FREE',
};

const SAFETY_SEVERITY_LABELS: Record<number, string> = {
  0: 'INFO',
  1: 'WARNING',
  2: 'CRITICAL',
  3: 'MRC REQUIRED',
};

const ODD_HEALTH_LABELS: Record<number, string> = {
  0: 'UNKNOWN',
  1: 'NORMAL',
  2: 'DEGRADED',
  3: 'FAIL',
};

const BEHAVIOR_LABELS: Record<number, string> = {
  0: 'TRANSIT',
  1: 'COLREG_AVOID',
  2: 'STATION_KEEP',
  3: 'MRC',
};

const BOTTOM_MODULE_TITLES: Record<ModuleName, string> = {
  M1: 'M1 - ODD 运行包络与状态机',
  M2: 'M2 - 世界模型与会遇度量',
  M3: 'M3 - 航次计划与调度跟踪',
  M4: 'M4 - IvP 行为仲裁决策细节',
  M5: 'M5 - MPC 战术轨迹收敛性',
  M6: 'M6 - COLREGs 规则与责任',
  M7: 'M7 - SOTIF 安全检查度量',
  M8: 'M8 - HMI 报警发布器状态',
};

function formatNumber(value: number | undefined, digits = 0): string | null {
  return value !== undefined && Number.isFinite(value) ? value.toFixed(digits) : null;
}

function formatPercent(value: number | undefined, digits = 0): string {
  return value !== undefined && Number.isFinite(value) ? `${(value * 100).toFixed(digits)}%` : '—';
}

function formatRawPercent(value: number | undefined, digits = 1): string {
  return value !== undefined && Number.isFinite(value) ? `${value.toFixed(digits)}%` : '—';
}

function formatBehavior(behavior: number | undefined, fallback?: string | null): string {
  if (behavior !== undefined) return BEHAVIOR_LABELS[behavior] ?? `BEHAVIOR ${behavior}`;
  return fallback || '—';
}

function formatDecisionRule(colregsRuleId: number | undefined, satRule: string | undefined, fsmRule: string): string {
  if (colregsRuleId !== undefined) return `Rule ${colregsRuleId}`;
  if (satRule) return satRule;
  if (fsmRule && fsmRule !== 'N/A' && fsmRule !== 'Nominal autopilot') return fsmRule;
  return '—';
}

function formatManeuverCommand(preferredDirection: string | undefined, minAlterationDeg: number | undefined, headingMinDeg: number | undefined, headingMaxDeg: number | undefined): string {
  if (preferredDirection) {
    const alteration = formatNumber(minAlterationDeg, 0);
    return alteration ? `${preferredDirection} ${alteration}°` : preferredDirection;
  }
  const headingMin = formatNumber(headingMinDeg, 0);
  const headingMax = formatNumber(headingMaxDeg, 0);
  if (headingMin && headingMax) return `${headingMin}°-${headingMax}°`;
  return '—';
}

function formatM5Command(status: string | undefined, targetSpeedKn: number | undefined, speedMinKn: number | undefined, speedMaxKn: number | undefined): string {
  const targetSpeed = formatNumber(targetSpeedKn, 1);
  if (status && targetSpeed) return `${status} / ${targetSpeed} kn`;
  if (status) return status;
  const speedMin = formatNumber(speedMinKn, 1);
  const speedMax = formatNumber(speedMaxKn, 1);
  if (speedMin && speedMax) return `${speedMin}-${speedMax} kn`;
  return '—';
}

function formatSafetyAlarm(severity: number | undefined, recommendedMrm: string | undefined, torActive: boolean): string {
  if (severity !== undefined) {
    const severityLabel = SAFETY_SEVERITY_LABELS[severity] ?? `SEV ${severity}`;
    return recommendedMrm ? `${severityLabel} / ${recommendedMrm}` : severityLabel;
  }
  return torActive ? '接管请求 (TOR)' : '—';
}

function findColregsChainConclusion(chain: Array<{ layer?: number; label?: string; conclusion?: string }> | undefined, matcher: (entry: { layer?: number; label?: string; conclusion?: string }) => boolean): string | undefined {
  return chain?.find((entry) => matcher(entry) && typeof entry.conclusion === 'string')?.conclusion;
}

function extractSatRule(chain: Array<{ layer?: number; label?: string; conclusion?: string }> | undefined): string | undefined {
  return findColregsChainConclusion(chain, (entry) => /rule\s*\d+/i.test(entry.conclusion ?? ''))
    ?? findColregsChainConclusion(chain, (entry) => /rule|规则|encounter|会遇/i.test(entry.label ?? ''));
}

function extractSatRole(chain: Array<{ layer?: number; label?: string; conclusion?: string }> | undefined): string | undefined {
  const role = findColregsChainConclusion(chain, (entry) => /give|stand|role|duty|责任/i.test(`${entry.label ?? ''} ${entry.conclusion ?? ''}`));
  if (!role) return undefined;
  if (/give[_ -]?way/i.test(role)) return 'GIVE-WAY 让路';
  if (/stand[_ -]?on/i.test(role)) return 'STAND-ON 保向';
  return role;
}

function formatBestIvpContribution(contributions: Array<{ direction_deg?: number; cost?: number; label?: string }> | undefined): string {
  if (!contributions?.length) return '—';
  const best = contributions
    .filter((entry) => Number.isFinite(entry.direction_deg) && Number.isFinite(entry.cost))
    .sort((a, b) => (a.cost ?? Infinity) - (b.cost ?? Infinity))[0];
  if (!best) return '—';
  const label = best.label ? ` ${best.label}` : '';
  return `${best.direction_deg?.toFixed(0)}° / ${best.cost?.toFixed(2)}${label}`;
}

function formatTrajectoryCount(avoidancePlan: { waypoints: unknown[] } | null | undefined, sat3: { trajectory_candidates?: unknown[] } | null | undefined): string {
  if (avoidancePlan) return `${avoidancePlan.waypoints.length} 条`;
  const candidates = sat3?.trajectory_candidates;
  return candidates?.length ? `${candidates.length} 候选` : '—';
}

function formatBestTrajectoryCost(sat3: { trajectory_candidates?: Array<{ cost?: number; is_optimal?: boolean; type?: string }> } | null | undefined): string {
  const candidates = sat3?.trajectory_candidates;
  if (!candidates?.length) return '—';
  const best = candidates.find((entry) => entry.is_optimal) ?? candidates[0];
  const cost = typeof best.cost === 'number' && Number.isFinite(best.cost) ? best.cost.toFixed(2) : '—';
  return best.type ? `${best.type} / ${cost}` : cost;
}

function formatLifecycleState(state: number | undefined): string {
  if (state === undefined) return '—';
  return {
    0: 'UNKNOWN',
    1: 'UNCONFIGURED',
    2: 'INACTIVE',
    3: 'ACTIVE',
    4: 'DEACTIVATING',
    5: 'FINALIZED',
  }[state] ?? `STATE ${state}`;
}

function computeRouteProgress(
  waypoints: Array<{ lat: number; lon: number }>,
  ownLat: number,
  ownLon: number,
  speedProfileKn: number[] | undefined,
  cruiseSpeed: number,
): RouteProgress | null {
  if (waypoints.length < 2) return null;

  const segmentDistances = waypoints.slice(0, -1).map((wp, idx) => (
    computeRangeNm(wp.lat, wp.lon, waypoints[idx + 1].lat, waypoints[idx + 1].lon)
  ));
  const totalDistanceNm = segmentDistances.reduce((sum, dist) => sum + dist, 0);
  if (totalDistanceNm <= 0) return null;

  const latScale = 60.0;
  const lonScale = 60.0 * Math.cos((ownLat * Math.PI) / 180);
  const toLocal = (wp: { lat: number; lon: number }) => ({
    x: (wp.lon - ownLon) * lonScale,
    y: (wp.lat - ownLat) * latScale,
  });

  let bestSegment = 0;
  let bestT = 0;
  let bestDist2 = Number.POSITIVE_INFINITY;

  for (let idx = 0; idx < waypoints.length - 1; idx += 1) {
    const a = toLocal(waypoints[idx]);
    const b = toLocal(waypoints[idx + 1]);
    const vx = b.x - a.x;
    const vy = b.y - a.y;
    const len2 = vx * vx + vy * vy;
    const rawT = len2 > 0 ? -(a.x * vx + a.y * vy) / len2 : 0;
    const t = Math.max(0, Math.min(1, rawT));
    const px = a.x + vx * t;
    const py = a.y + vy * t;
    const dist2 = px * px + py * py;
    if (dist2 < bestDist2) {
      bestDist2 = dist2;
      bestSegment = idx;
      bestT = t;
    }
  }

  const activeSegment = bestT >= 0.98 && bestSegment + 1 < segmentDistances.length
    ? bestSegment + 1
    : bestSegment;
  const nextWaypointIndex = Math.min(activeSegment + 1, waypoints.length - 1);
  const remainingFirstSegmentNm = segmentDistances[activeSegment] * (activeSegment === bestSegment ? (1 - bestT) : 1);

  let remainingDistanceNm = remainingFirstSegmentNm;
  let remainingTimeS = remainingFirstSegmentNm / segmentSpeedKn(speedProfileKn, activeSegment, cruiseSpeed) * 3600;

  for (let idx = activeSegment + 1; idx < segmentDistances.length; idx += 1) {
    const distNm = segmentDistances[idx];
    remainingDistanceNm += distNm;
    remainingTimeS += distNm / segmentSpeedKn(speedProfileKn, idx, cruiseSpeed) * 3600;
  }

  return {
    nextWaypointIndex,
    remainingDistanceNm,
    remainingTimeS,
    plannedSpeedKn: segmentSpeedKn(speedProfileKn, activeSegment, cruiseSpeed),
  };
}

export function SimulationMonitor({ routeScenarioId }: SimulationMonitorProps = {}) {
  const [activeRightTab, setActiveRightTab] = useState<RightTabId | null>(null);
  const [activeBottomModule, setActiveBottomModule] = useState<string | null>(null);
  const [radarRangeNM, setRadarRangeNM] = useState<RadarRangeNm>(10);
  const previousAvoidancePhaseRef = useRef<AvoidancePhase | null>(null);
  const wsUrl = `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}/foxglove-ws`;
  useFoxgloveLive(wsUrl, true);

  const lifecycleStatus = useTelemetryStore((s) => s.lifecycleStatus);
  const { data: lifecycleStatusHttp } = useGetLifecycleStatusQuery(undefined, {
    pollingInterval: 1000,
  });
  const asdrEvents      = useTelemetryStore((s) => s.asdrEvents);
  const wsConnected     = useTelemetryStore((s) => s.wsConnected);
  const ownShip         = useTelemetryStore((s) => s.ownShip);
  const targets         = useTelemetryStore((s) => s.targets);
  const modulePulses    = useTelemetryStore((s) => s.modulePulses);
  const sat2            = useTelemetryStore((s) => s.sat2);
  const sat3            = useTelemetryStore((s) => s.sat3);
  const sotifMetrics    = useTelemetryStore((s) => s.sotifMetrics);
  const scoringRow      = useTelemetryStore((s) => s.scoringRow);
  const oddState        = useTelemetryStore((s) => s.oddState);
  const behaviorPlan    = useTelemetryStore((s) => s.behaviorPlan);
  const colregsConstraint = useTelemetryStore((s) => s.colregsConstraint);
  const safetyAlert     = useTelemetryStore((s) => s.safetyAlert);
  const avoidancePlan   = useTelemetryStore((s) => s.avoidancePlan);
  const threatState     = useTelemetryStore((s) => s.threatState);
  const threatRiskHistory = useTelemetryStore((s) => s.threatRiskHistory);
  const fsmState        = useFsmStore((s) => s.currentState);
  const fsmRule         = useFsmStore((s) => s.activeRule);
  const fsmConf         = useFsmStore((s) => s.confidence);
  const fsmHistory      = useFsmStore((s) => s.transitionHistory);
  const torRequest      = useFsmStore((s) => s.torRequest);
  const isSat2Stale     = useTelemetryStore((s) => s.isSat2Stale);
  const isSat3Stale     = useTelemetryStore((s) => s.isSat3Stale);
  const isSotifMetricsStale = useTelemetryStore((s) => s.isSotifMetricsStale);
  const ownShipTrail        = useTelemetryStore((s) => s.ownShipTrail);
  const simRate             = useControlStore((s) => s.simRate);

  const storedScenarioId = useScenarioStore((s) => s.scenarioId);
  const scenarioId = routeScenarioId || storedScenarioId || lifecycleStatus?.scenario_id;
  const localLifecycleScenarioId = lifecycleStatus?.scenario_id;
  const authoritativeLifecycleScenarioId = (lifecycleStatusHttp as any)?.scenario_id ?? lifecycleStatusHttp?.scenarioId;
  const localLifecycleMatchesRoute = Boolean(routeScenarioId && localLifecycleScenarioId === routeScenarioId);
  const routeLifecycleMismatch = Boolean(
    routeScenarioId
    && !localLifecycleMatchesRoute
    && authoritativeLifecycleScenarioId
    && authoritativeLifecycleScenarioId !== routeScenarioId,
  );
  const { data: activeScenario } = useGetScenarioQuery(scenarioId ?? '', { skip: !scenarioId });

  useEffect(() => {
    if (!routeLifecycleMismatch || !routeScenarioId) return;
    useTelemetryStore.getState().reset();
    useControlStore.getState().reset();
    window.location.hash = `#/check/${routeScenarioId}`;
  }, [routeLifecycleMismatch, routeScenarioId]);
  
  // Standardize Scenario YAML route waypoints extraction to the exact same `{ lat, lon }` structure
  const yamlRoute = useMemo<VoyagePlanData | null>(() => {
    if (!activeScenario?.yaml_content) return null;
    try {
      const doc = jsyaml.load(activeScenario.yaml_content) as any;
      const rawWpts = doc?.ownShip?.nominalRoute || doc?.voyageTask?.waypoints || doc?.mock_l2?.planned_route?.waypoints || [];
      const waypoints = rawWpts.map((wp: any) => ({
        lat: wp.latitude ?? wp.lat ?? 0.0,
        lon: wp.longitude ?? wp.lon ?? 0.0,
      }));
      const cruiseSpeed = doc?.ownShip?.nominalRoute?.[0]?.target_sog_kn || 
                          doc?.voyageTask?.cruise_speed_kn || 
                          doc?.mock_l2?.planned_route?.cruise_speed_kn || 
                          10.0;
      return {
        waypoints,
        cruiseSpeed,
        speedProfileKn: rawWpts.slice(0, Math.max(0, rawWpts.length - 1)).map((wp: any) => (
          finiteNumber(wp.target_sog_kn ?? wp.speed_kn) ?? cruiseSpeed
        )),
        source: 'static_yaml' as const,
      };
    } catch {
      return null;
    }
  }, [activeScenario]);

  // Hybrid fallback resolution
  const l2Plan = useTelemetryStore((s) => s.voyagePlan);
  const voyagePlan = l2Plan || yamlRoute;

  const planDetails = useMemo(() => {
    if (!voyagePlan?.waypoints?.length) return null;
    const ownLat = ownShip?.pose?.lat;
    const ownLon = ownShip?.pose?.lon;

    const validWaypoints = voyagePlan.waypoints.filter((wp: { lat: number; lon: number }) => (
      finiteNumber(wp.lat) !== null && finiteNumber(wp.lon) !== null
    ));
    const cruiseSpeed = voyagePlan.cruiseSpeed || 10.0;
    const progress = ownLat != null && ownLon != null
      ? computeRouteProgress(validWaypoints, ownLat, ownLon, voyagePlan.speedProfileKn, cruiseSpeed)
      : null;
    const fallbackDistanceNm = finiteNumber(voyagePlan.totalDistanceNm);
    const fallbackDurationS = finiteNumber(voyagePlan.estimatedDurationS);

    const remainingTimeS = progress?.remainingTimeS ?? fallbackDurationS;
    const etaString = formatRemainingSimTime(remainingTimeS);

    const nextWaypointIndex = progress?.nextWaypointIndex ?? Math.min(1, validWaypoints.length - 1);
    const distanceNm = progress?.remainingDistanceNm ?? fallbackDistanceNm;
    const plannedSpeedKn = progress?.plannedSpeedKn ?? segmentSpeedKn(voyagePlan.speedProfileKn, 0, cruiseSpeed);
    const wptName = `WP${String(nextWaypointIndex + 1).padStart(2, '0')}`;
    const plannedSpeed = `${plannedSpeedKn.toFixed(1)} kn`;

    return {
      wpt: wptName,
      dist: distanceNm !== null ? `${distanceNm.toFixed(1)} nm` : '—',
      eta: etaString,
      spd: plannedSpeed,
      source: voyagePlan.source,
    };
  }, [ownShip, voyagePlan]);

  const avoidanceDecisionDetails = useMemo(() => {
    const satRule = extractSatRule(sat2?.colregs_chain);
    const satRole = extractSatRole(sat2?.colregs_chain);
    const firstAvoidanceWaypoint = avoidancePlan?.waypoints?.[0];
    const envelope = oddState?.envelopeState !== undefined
      ? ODD_ENVELOPE_LABELS[oddState.envelopeState] ?? `ODD ${oddState.envelopeState}`
      : '—';
    const role = colregsConstraint?.role !== undefined
      ? COLREGS_ROLE_LABELS[colregsConstraint.role] ?? `ROLE ${colregsConstraint.role}`
      : satRole ?? '—';

    return {
      envelope,
      rule: formatDecisionRule(colregsConstraint?.ruleId, satRule, fsmRule),
      role,
      maneuver: formatManeuverCommand(
        colregsConstraint?.preferredDirection,
        colregsConstraint?.minAlterationDeg,
        behaviorPlan?.headingMinDeg,
        behaviorPlan?.headingMaxDeg,
      ),
      m5Command: formatM5Command(
        avoidancePlan?.status,
        firstAvoidanceWaypoint?.targetSpeedKn,
        behaviorPlan?.speedMinKn,
        behaviorPlan?.speedMaxKn,
      ),
      alarm: formatSafetyAlarm(safetyAlert?.severity, safetyAlert?.recommendedMrm, Boolean(torRequest)),
    };
  }, [avoidancePlan, behaviorPlan, colregsConstraint, fsmRule, oddState, safetyAlert, sat2, torRequest]);

  const nearestTargetMetrics = useMemo(() => {
    let best: { cpaNm: number | null; tcpaMin: number | null } | null = null;
    for (const target of targets) {
      const targetMetrics = target as typeof target & { cpaM?: number; tcpaS?: number };
      const cpaNm = typeof targetMetrics.cpaM === 'number' ? targetMetrics.cpaM / 1852.0 : null;
      const tcpaMin = typeof targetMetrics.tcpaS === 'number' ? targetMetrics.tcpaS / 60.0 : null;
      if (cpaNm === null && tcpaMin === null) continue;
      if (!best || (cpaNm !== null && (best.cpaNm === null || cpaNm < best.cpaNm))) {
        best = { cpaNm, tcpaMin };
      }
    }
    return best;
  }, [targets]);

  const moduleRealtimeRows = useMemo(() => {
    const firstAvoidanceWaypoint = avoidancePlan?.waypoints?.[0];
    const speedMin = formatNumber(behaviorPlan?.speedMinKn, 1);
    const speedMax = formatNumber(behaviorPlan?.speedMaxKn, 1);
    const speedWindow = speedMin && speedMax ? `${speedMin}-${speedMax} kn` : '—';
    const m4Behavior = formatBehavior(behaviorPlan?.behavior, sat2?.active_behavior);
    const m4Confidence = behaviorPlan?.confidence ?? sat2?.active_behavior_weight;
    const m5Status = avoidancePlan?.status ?? (sat3?.trajectory_candidates?.length ? 'SAT3 CANDIDATE' : undefined);
    const safetySeverity = safetyAlert?.severity !== undefined
      ? SAFETY_SEVERITY_LABELS[safetyAlert.severity] ?? `SEV ${safetyAlert.severity}`
      : '—';
    const sotifStatus = sotifMetrics ? `SOTIF ${formatRawPercent(sotifMetrics.checker_veto_rate_pct)}` : '—';

    return {
      M1: [
        { label: 'ODD 包络边界', value: avoidanceDecisionDetails.envelope },
        { label: '一致性评分', value: formatPercent(oddState?.conformanceScore) },
        { label: '健康状态', value: oddState?.health !== undefined ? (ODD_HEALTH_LABELS[oddState.health] ?? `HEALTH ${oddState.health}`) : '—' },
        { label: 'FSM 阶段', value: fsmState.replace(/_/g, ' ') },
        { label: '生命周期', value: formatLifecycleState(lifecycleStatus?.current_state) },
      ],
      M2: [
        { label: '目标数量', value: `${targets.length}` },
        { label: '最近会遇 CPA', value: nearestTargetMetrics?.cpaNm !== null && nearestTargetMetrics?.cpaNm !== undefined ? `${nearestTargetMetrics.cpaNm.toFixed(2)} nm` : '—' },
        { label: '最近会遇 TCPA', value: nearestTargetMetrics?.tcpaMin !== null && nearestTargetMetrics?.tcpaMin !== undefined ? `${nearestTargetMetrics.tcpaMin.toFixed(1)} min` : '—' },
        { label: '世界模型状态', value: wsConnected ? 'LIVE' : 'DOWN' },
      ],
      M3: [
        { label: '计划路点 WPT', value: planDetails?.wpt ?? '—' },
        { label: '终点距离 DIST', value: planDetails?.dist ?? '—' },
        { label: '剩余航时 ETA', value: planDetails?.eta ?? '—' },
        { label: '计划速度 SPD', value: planDetails?.spd ?? '—' },
      ],
      M4: [
        { label: '仲裁行为', value: m4Behavior },
        { label: '转向窗口', value: formatManeuverCommand(undefined, undefined, behaviorPlan?.headingMinDeg, behaviorPlan?.headingMaxDeg) },
        { label: '方向代价', value: formatBestIvpContribution(sat2?.ivp_contributions) },
        { label: '速度窗口', value: speedWindow },
        { label: '置信度/权重', value: formatPercent(m4Confidence) },
      ],
      M5: [
        { label: '指令输出', value: formatM5Command(m5Status, firstAvoidanceWaypoint?.targetSpeedKn, behaviorPlan?.speedMinKn, behaviorPlan?.speedMaxKn) },
        { label: '路径点/候选', value: formatTrajectoryCount(avoidancePlan, sat3) },
        { label: '规划时域', value: avoidancePlan?.horizonS !== undefined ? `${avoidancePlan.horizonS.toFixed(0)} s` : '—' },
        { label: '最优代价', value: formatBestTrajectoryCost(sat3) },
        { label: '置信度', value: formatPercent(avoidancePlan?.confidence) },
      ],
      M6: [
        { label: '避碰规则', value: avoidanceDecisionDetails.rule },
        { label: '责任角色', value: avoidanceDecisionDetails.role },
        { label: '目标 MMSI', value: sat2?.colregs_chain_target_id ?? '—' },
        { label: '机动方向', value: avoidanceDecisionDetails.maneuver },
        { label: '推理阶段/延迟', value: colregsConstraint?.phase ?? (sat2?.reasoning_latency_ms !== undefined ? `${sat2.reasoning_latency_ms.toFixed(1)} ms` : '—') },
      ],
      M7: [
        { label: '安全告警', value: avoidanceDecisionDetails.alarm !== '—' ? avoidanceDecisionDetails.alarm : sotifStatus },
        { label: '检查器否决率', value: formatRawPercent(sotifMetrics?.checker_veto_rate_pct) },
        { label: '感知覆盖率', value: formatRawPercent(sotifMetrics?.perception_coverage_pct) },
        { label: 'SOTIF 描述', value: safetyAlert?.description ?? '—' },
        { label: '告警置信度', value: formatPercent(safetyAlert?.confidence) },
      ],
      M8: [
        { label: '实时链路', value: wsConnected ? 'LIVE' : 'DOWN' },
        { label: '交互 RTT', value: sotifMetrics?.comm_link_rtt_ms !== undefined ? `${sotifMetrics.comm_link_rtt_ms.toFixed(0)} ms` : '—' },
        { label: '感知覆盖率', value: formatRawPercent(sotifMetrics?.perception_coverage_pct) },
        { label: '报警等级', value: safetySeverity },
      ],
    } satisfies Record<ModuleName, ModuleDetailRow[]>;
  }, [
    avoidanceDecisionDetails,
    avoidancePlan,
    behaviorPlan,
    colregsConstraint,
    fsmState,
    lifecycleStatus,
    nearestTargetMetrics,
    oddState,
    planDetails,
    safetyAlert,
    sat2,
    sat3,
    sotifMetrics,
    targets.length,
    wsConnected,
  ]);

  const moduleSummaries = useMemo<Record<ModuleName, string>>(() => ({
    M1: avoidanceDecisionDetails.envelope !== '—' ? `ODD ${avoidanceDecisionDetails.envelope}` : fsmState.replace(/_/g, ' '),
    M2: targets.length > 0 ? `${targets.length} tgt` : '—',
    M3: planDetails?.wpt ? `WPT ${planDetails.wpt}` : '—',
    M4: formatBehavior(behaviorPlan?.behavior, sat2?.active_behavior).replace('COLREG_AVOID', 'COLREG'),
    M5: avoidancePlan?.status ?? (avoidancePlan ? `${avoidancePlan.waypoints.length} wpt` : (sat3?.trajectory_candidates?.length ? `${sat3.trajectory_candidates.length} cand` : '—')),
    M6: colregsConstraint?.ruleId !== undefined ? `R${colregsConstraint.ruleId}` : avoidanceDecisionDetails.rule,
    M7: safetyAlert?.severity !== undefined ? `ALM ${SAFETY_SEVERITY_LABELS[safetyAlert.severity] ?? safetyAlert.severity}` : (sotifMetrics ? 'SOTIF' : '—'),
    M8: wsConnected ? 'WS LIVE' : 'WS DOWN',
  }), [avoidanceDecisionDetails, avoidancePlan, behaviorPlan, colregsConstraint, fsmState, planDetails, safetyAlert, sat2, sat3, sotifMetrics, targets.length, wsConnected]);

  // Scenario Switch Protection: Reset active real-time L2 plan inside telemetry store on scenario change
  const prevScenarioIdRef = useRef<string | null>(null);
  useEffect(() => {
    if (prevScenarioIdRef.current !== null && prevScenarioIdRef.current !== scenarioId) {
      useTelemetryStore.getState().updateVoyagePlan(null);
    }
    prevScenarioIdRef.current = scenarioId || null;
  }, [scenarioId]);

  const targetRiskById = useMemo(() => {
    const map = new Map<string, ThreatRiskTargetData>();
    for (const risk of threatState?.targets ?? []) {
      map.set(risk.targetId, risk);
    }
    return map;
  }, [threatState]);

  // Stable target list: each target stays in one card; backend risk is a badge.
  const targetsWithRisk = useMemo(() => (
    targets.map((target) => ({
      ...target,
      risk: targetRiskById.get(String(target.mmsi)),
    }))
  ), [targetRiskById, targets]);

  const riskPhaseColor = (phase?: string) => {
    if (phase === 'Critical' || phase === 'Danger') return 'var(--c-danger)';
    if (phase === 'Warning') return 'var(--c-warn)';
    if (phase === 'Monitor') return '#38bdf8';
    return 'var(--c-phos)';
  };

  const renderThreatRiskTrend = (history: ThreatRiskHistorySample[]) => {
    const points = history.slice(-32).filter((sample) => Number.isFinite(sample.riskScore));
    if (points.length === 0) {
      return null;
    }
    const latest = points[points.length - 1];
    const width = 300;
    const height = 78;
    const pad = 10;
    const usableWidth = width - pad * 2;
    const usableHeight = height - pad * 2;
    const pathPoints = points.map((sample, idx) => {
      const x = pad + (points.length === 1 ? usableWidth : (idx / (points.length - 1)) * usableWidth);
      const clamped = Math.max(0, Math.min(1, sample.riskScore));
      const y = pad + (1 - clamped) * usableHeight;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    }).join(' ');
    const latestColor = latest.riskPhase === 'Critical' || latest.riskPhase === 'Danger'
      ? 'var(--c-danger)'
      : latest.riskPhase === 'Warning'
        ? 'var(--c-warn)'
        : latest.riskPhase === 'Monitor'
          ? '#38bdf8'
          : 'var(--c-phos)';

    return (
      <div style={{
        background: 'rgba(0,0,0,0.2)',
        border: '1px solid var(--line-1)',
        borderRadius: 8,
        padding: '12px 14px',
        display: 'flex',
        flexDirection: 'column',
        gap: 10,
      }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 12 }}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.08em' }}>后端风险趋势</span>
            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>
              Primary {latest.primaryTargetId || threatState?.primaryTargetId || '—'}
            </span>
          </div>
          <div style={{ textAlign: 'right' }}>
            <div style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: latestColor, fontWeight: 800, lineHeight: 1 }}>
              {latest.riskScore.toFixed(2)}
            </div>
            <div style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)', marginTop: 3 }}>
              {latest.riskPhase}
            </div>
          </div>
        </div>
        <svg
          data-testid="threat-risk-trend"
          viewBox={`0 0 ${width} ${height}`}
          role="img"
          aria-label="后端 primary threat risk score trend"
          style={{ width: '100%', height: 78, display: 'block' }}
        >
          <line x1={pad} y1={pad} x2={pad} y2={height - pad} stroke="rgba(148,163,184,0.22)" strokeWidth="1" />
          <line x1={pad} y1={height - pad} x2={width - pad} y2={height - pad} stroke="rgba(148,163,184,0.22)" strokeWidth="1" />
          <line x1={pad} y1={pad + usableHeight * 0.4} x2={width - pad} y2={pad + usableHeight * 0.4} stroke="rgba(251,191,36,0.22)" strokeWidth="1" strokeDasharray="3 4" />
          <line x1={pad} y1={pad + usableHeight * 0.15} x2={width - pad} y2={pad + usableHeight * 0.15} stroke="rgba(244,63,94,0.22)" strokeWidth="1" strokeDasharray="3 4" />
          <polyline
            points={pathPoints}
            fill="none"
            stroke={latestColor}
            strokeWidth="2.4"
            strokeLinecap="round"
            strokeLinejoin="round"
          />
          {points.map((sample, idx) => {
            const x = pad + (points.length === 1 ? usableWidth : (idx / (points.length - 1)) * usableWidth);
            const clamped = Math.max(0, Math.min(1, sample.riskScore));
            const y = pad + (1 - clamped) * usableHeight;
            return <circle key={`${sample.t}-${idx}`} cx={x} cy={y} r={idx === points.length - 1 ? 3.5 : 2} fill={idx === points.length - 1 ? latestColor : 'rgba(94,234,212,0.65)'} />;
          })}
        </svg>
      </div>
    );
  };

  const renderTargetCards = (targetsList: any[]) => {
    // Derive CPA/TCPA from ASDR events
    const cpaMap = new Map<string, { cpa: number; tcpa: number }>();
    for (const e of asdrEvents) {
      if (e.event_type === 'cpa_update' && e.payload_json) {
        try {
          const p = JSON.parse(e.payload_json);
          if (p.mmsi !== undefined && p.cpa_nm !== undefined) {
            cpaMap.set(String(p.mmsi), { cpa: p.cpa_nm, tcpa: p.tcpa_min ?? 0 });
          }
        } catch { /* noop */ }
      }
    }

    const ownLat = ownShip?.pose?.lat;
    const ownLon = ownShip?.pose?.lon;

    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
        {targetsList.map((t, idx) => {
          const id = t.mmsi ? String(t.mmsi) : `T${idx + 1}`;
          const targetIdDisplay = t.mmsi ? `T${String(t.mmsi).slice(-2)}` : `T${idx + 1}`;
          const cpaInfo = cpaMap.get(id) ?? cpaMap.get('*');
          const risk = t.risk as ThreatRiskTargetData | undefined;

          const targetLat = t.pose?.lat;
          const targetLon = t.pose?.lon;
          const cpaFallback = (
            ownLat != null && ownLon != null && targetLat != null && targetLon != null
          ) ? computeCpaTcpa({
              own: {
                lat: ownLat,
                lon: ownLon,
                sogMps: ownShip?.kinematics?.sog ?? 0,
                cogRad: ownShip?.kinematics?.cog ?? ownShip?.pose?.heading ?? 0,
              },
              target: {
                lat: targetLat,
                lon: targetLon,
                sogMps: t.kinematics?.sog ?? 0,
                cogRad: t.kinematics?.cog ?? t.pose?.heading ?? 0,
              },
            }) : null;
          const cpaVal = risk?.dcpaM != null
            ? risk.dcpaM / 1852.0
            : typeof t.cpaM === 'number'
              ? t.cpaM / 1852.0
              : (cpaInfo?.cpa ?? (cpaFallback ? cpaFallback.cpaM / 1852.0 : undefined));
          const tcpaVal = risk?.tcpaS != null
            ? risk.tcpaS / 60.0
            : typeof t.tcpaS === 'number'
              ? t.tcpaS / 60.0
              : (cpaInfo?.tcpa ?? (cpaFallback ? cpaFallback.tcpaS / 60.0 : undefined));

          const brg = (ownLat != null && ownLon != null && targetLat != null && targetLon != null)
            ? computeBearing(ownLat, ownLon, targetLat, targetLon).toFixed(1) + '°'
            : '—';

          const rng = (ownLat != null && ownLon != null && targetLat != null && targetLon != null)
            ? computeRangeNm(ownLat, ownLon, targetLat, targetLon).toFixed(2) + ' nm'
            : '—';

          const hdg = t.pose?.heading != null 
            ? `${((t.pose.heading * 180 / Math.PI + 360) % 360).toFixed(1)}°`
            : (t.kinematics?.cog != null ? `${((t.kinematics.cog * 180 / Math.PI + 360) % 360).toFixed(0)}°` : '—');

          const sog = t.kinematics?.sog != null 
            ? `${(t.kinematics.sog * 1.944).toFixed(1)} kn`
            : '—';

          const cpa = cpaVal != null ? `${cpaVal.toFixed(2)} nm` : '—';
          const tcpa = tcpaVal != null ? `${tcpaVal.toFixed(1)} min` : '—';

          const cpaColor = cpaVal != null
            ? cpaVal < 1.0 ? 'var(--c-danger)' : cpaVal < 2.0 ? 'var(--c-warn)' : '#fff'
            : '#fff';
          const riskScore = risk?.riskScore != null ? risk.riskScore.toFixed(2) : '—';
          const warningMargin = risk?.warningMarginM != null ? `${risk.warningMarginM.toFixed(0)} m` : '—';
          const dangerMargin = risk?.dangerMarginM != null ? `${risk.dangerMarginM.toFixed(0)} m` : '—';
          const riskPhase = risk?.riskPhase ?? 'Clear';
          const riskStatus = risk?.primary ? 'PRIMARY' : risk ? 'TRACK' : 'CLEAR';
          const riskColor = riskPhaseColor(riskPhase);

          return (
            <div key={id} style={{
              background: 'rgba(0,0,0,0.15)',
              border: '1px solid var(--line-1)',
              borderRadius: 8,
              padding: '12px 14px',
              display: 'flex',
              flexDirection: 'column',
              gap: 12
            }}>
              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 12 }}>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                  <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>目标状态</span>
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--txt-2)', fontWeight: 700 }}>
                    {risk?.targetId ? `Primary ${risk.targetId}` : id}
                  </span>
                </div>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6, flexWrap: 'wrap', justifyContent: 'flex-end' }}>
                  <span style={{
                    background: `${riskColor}22`,
                    border: `1px solid ${riskColor}66`,
                    color: riskColor,
                    padding: '2px 6px',
                    borderRadius: 4,
                    fontFamily: 'var(--f-mono)',
                    fontSize: 9,
                    fontWeight: 800,
                    letterSpacing: '0.04em',
                  }}>
                    {riskPhase.toUpperCase()}
                  </span>
                  <span style={{
                    background: risk?.primary ? 'rgba(45,212,191,0.16)' : 'rgba(148,163,184,0.12)',
                    border: risk?.primary ? '1px solid rgba(45,212,191,0.45)' : '1px solid rgba(148,163,184,0.25)',
                    color: risk?.primary ? 'var(--c-phos)' : 'var(--txt-2)',
                    padding: '2px 6px',
                    borderRadius: 4,
                    fontFamily: 'var(--f-mono)',
                    fontSize: 9,
                    fontWeight: 800,
                    letterSpacing: '0.04em',
                  }}>
                    {riskStatus}
                  </span>
                </div>
              </div>

              {/* Row 1: ID and MMSI */}
	              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px 16px' }}>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                  <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>目标 ID</span>
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: 'var(--c-info)', fontWeight: 700, lineHeight: 1.1 }}>
                    {targetIdDisplay}
                  </span>
                </div>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                  <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>呼号 MMSI</span>
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                    {t.mmsi || '—'}
                  </span>
	              </div>
	              {risk && (
	                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px 16px' }}>
	                  <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
	                    <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>风险阶段</span>
	                    <span style={{ fontFamily: 'var(--f-mono)', fontSize: 16, color: risk.riskPhase === 'Critical' || risk.riskPhase === 'Danger' ? 'var(--c-danger)' : risk.riskPhase === 'Warning' ? 'var(--c-warn)' : '#38bdf8', fontWeight: 700, lineHeight: 1.1 }}>
	                      {risk.riskPhase}
	                    </span>
	                  </div>
	                  <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
	                    <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>风险分数</span>
	                    <span style={{ fontFamily: 'var(--f-mono)', fontSize: 16, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
	                      {riskScore}
	                    </span>
	                  </div>
	                  <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
	                    <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>Warning Margin</span>
	                    <span style={{ fontFamily: 'var(--f-mono)', fontSize: 16, color: risk.warningMarginM != null && risk.warningMarginM < 0 ? 'var(--c-warn)' : '#fff', fontWeight: 700, lineHeight: 1.1 }}>
	                      {warningMargin}
	                    </span>
	                  </div>
	                  <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
	                    <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>Danger Margin</span>
	                    <span style={{ fontFamily: 'var(--f-mono)', fontSize: 16, color: risk.dangerMarginM != null && risk.dangerMarginM < 0 ? 'var(--c-danger)' : '#fff', fontWeight: 700, lineHeight: 1.1 }}>
	                      {dangerMargin}
	                    </span>
	                  </div>
	                </div>
	              )}
            </div>

              {/* Row 2: BRG and RNG */}
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px 16px' }}>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                  <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>方位 BRG</span>
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                    {brg}
                  </span>
                </div>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                  <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>距离 RNG</span>
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                    {rng}
                  </span>
                </div>
              </div>

              {/* Row 3: HDG and SOG */}
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px 16px' }}>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                  <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>首向 HDG</span>
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                    {hdg}
                  </span>
                </div>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                  <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>航速 SOG</span>
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                    {sog}
                  </span>
                </div>
              </div>

              {/* Row 4: CPA and TCPA */}
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px 16px' }}>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                  <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>最近会遇 CPA</span>
                  <span data-testid="threat-cpa" style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: cpaColor, fontWeight: 700, lineHeight: 1.1 }}>
                    {cpa}
                  </span>
                </div>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                  <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>会遇时间 TCPA</span>
                  <span data-testid="threat-tcpa" style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                    {tcpa}
                  </span>
                </div>
              </div>
            </div>
          );
        })}
      </div>
    );
  };

  const isPaused  = useControlStore((s) => s.isPaused);
  const setSimRate = useControlStore((s) => s.setSimRate);
  const setPaused  = useControlStore((s) => s.setPaused);

  const viewMode       = useUIStore((s) => s.viewMode);
  const setViewMode    = useUIStore((s) => s.setViewMode);
  const leftDrawerOpen  = useUIStore((s) => s.leftDrawerOpen);
  const rightDrawerOpen = useUIStore((s) => s.rightDrawerOpen);
  const toggleLeft     = useUIStore((s) => s.toggleLeftDrawer);
  const toggleRight    = useUIStore((s) => s.toggleRightDrawer);
  const asdrExpanded   = useUIStore((s) => s.asdrLogExpanded);
  const toggleAsdr     = useUIStore((s) => s.toggleAsdrLog);

  const [showFaultModal, setShowFaultModal] = useState(false);
  const [substrate, setSubstrate] = useState<'enc' | 'sat' | 'osm'>('enc');
  const [deactivate] = useDeactivateLifecycleMutation();
  const [changeRate] = useChangeLifecycleRateMutation();
  const autoNavRef = useRef(false);
  const externalMapRef = useRef<maplibregl.Map | null>(null);

  const encRegion = useMemo(() => {
    if (!activeScenario?.yaml_content) {
      return 'trondelag';
    }
    try {
      const doc = jsyaml.load(activeScenario.yaml_content) as any;
      return doc?.metadata?.odd_cell?.domain === 'coastal_archipelago' ? 'coastal_archipelago' : 'trondelag';
    } catch {
      return 'trondelag';
    }
  }, [activeScenario]);

  const handlePlay = async () => {
    setPaused(false);
    await changeRate(simRate);
  };

  const handlePause = async () => {
    setPaused(true);
    await changeRate(0.0);
  };

  const handleRateChange = async (rate: number) => {
    setSimRate(rate);
    if (!isPaused) {
      await changeRate(rate);
    }
  };

  const handleRadarWheel = (event: React.WheelEvent<HTMLDivElement>) => {
    event.preventDefault();
    const currentIndex = RADAR_RANGES_NM.indexOf(radarRangeNM);
    const direction = event.deltaY > 0 ? 1 : -1;
    const nextIndex = Math.max(0, Math.min(RADAR_RANGES_NM.length - 1, currentIndex + direction));
    setRadarRangeNM(RADAR_RANGES_NM[nextIndex]);
  };

  useHotkeys({
    onSpace: () => {
      if (isPaused) {
        handlePlay();
      } else {
        handlePause();
      }
    },
    onToggleEngineer: () => {
      setViewMode(viewMode === 'engineer' ? 'captain' : 'engineer');
    },
    onToggleRoc: () => {
      setViewMode(viewMode === 'roc' ? 'captain' : 'roc');
    },
    onTor: () => {
      const fsm = useFsmStore.getState();
      if (fsm.currentState !== 'TOR') {
        fsm.setState('TOR', 'MANUAL_TOR_TRIGGER', simTimeSec);
        fsm.setTorRequest({
          reason: 'Manual Operator Intervention Requested (Collision Hazard)',
          triggeredAtSimTime: simTimeSec,
          tmrDeadlineSimTime: simTimeSec + 60,
          currentSituation: 'Target EVT14A040 CPA < 0.15 NM, own ship on autopilot',
          proposedAction: 'Captain/operator takeover to perform manual avoidance maneuver',
          recommendedMrm: 'MRM-01',
        });
      } else {
        fsm.setState('TRANSIT', 'MANUAL_TOR_CANCEL', simTimeSec);
        fsm.setTorRequest(null);
      }
    },
    onFault: () => {
      setActiveRightTab(activeRightTab === 'fault' ? null : 'fault');
    },
    onMrc: () => {
      const fsm = useFsmStore.getState();
      if (fsm.currentState !== 'MRC') {
        fsm.setState('MRC', 'MANUAL_MRC_TRIGGER', simTimeSec);
        fsm.setTorRequest(null);
      } else {
        fsm.setState('TRANSIT', 'MANUAL_MRC_CANCEL', simTimeSec);
      }
    },
    onArrowLeft:  () => {},
    onArrowRight: () => {},
    onArrowUp:    () => {},
    onArrowDown:  () => {},
    onHandback: () => {
      const fsm = useFsmStore.getState();
      fsm.setState('TRANSIT', 'MANUAL_HANDBACK', simTimeSec);
      fsm.setTorRequest(null);
    },
  });

  async function handleStop() {
    await deactivate();
    window.location.hash = '#/evaluator/latest';
  }

  const simTimeSec = lifecycleStatus?.sim_time ?? 0;
  const lcState    = lifecycleStatus?.current_state;
  const avoidancePhaseState = useMemo(() => deriveAvoidancePhaseState({
    simTimeSec,
    targets: targets.map((target) => {
      const t = target as typeof target & {
        cpaM?: number;
        tcpaS?: number;
        rngM?: number;
        brgDeg?: number;
        encounter?: string;
      };
      return {
        mmsi: t.mmsi,
        cpaM: t.cpaM,
        tcpaS: t.tcpaS,
        rngM: t.rngM,
        brgDeg: t.brgDeg,
        encounter: t.encounter,
      };
    }),
    oddState,
    colregsConstraint,
    behaviorPlan,
    avoidancePlan,
    safetyAlert,
    sat2,
    sat3,
    sotifMetrics,
    previousPhase: previousAvoidancePhaseRef.current,
  }), [
    avoidancePlan,
    behaviorPlan,
    colregsConstraint,
    oddState,
    safetyAlert,
    sat2,
    sat3,
    simTimeSec,
    sotifMetrics,
    targets,
  ]);

  useEffect(() => {
    previousAvoidancePhaseRef.current = avoidancePhaseState.phase;
  }, [avoidancePhaseState.phase]);

  useEffect(() => {
    if (lcState === 5 && !autoNavRef.current) {
      autoNavRef.current = true;
      const timer = setTimeout(() => { window.location.hash = '#/evaluator/latest'; }, 1500);
      return () => clearTimeout(timer);
    }
  }, [lcState]);

  // Dev-mode auto-switch to engineer view for E2E testing
  useEffect(() => {
    if (window.location.hash.includes('dev=1')) {
      useUIStore.getState().setViewMode('engineer');
    }
  }, []);

  const borderColor = FSM_BORDER[fsmState] ?? 'transparent';
  const boxShadow   = FSM_GLOW[fsmState] ?? 'none';
  const isEngineer  = viewMode === 'engineer';
  const isRoc       = viewMode === 'roc';
  const isCaptainPanelActive = activeRightTab === 'ship' || activeRightTab === 'threat' || activeRightTab === 'avoid';
  const isMonitorPanelActive = activeRightTab === 'asdr' || activeRightTab === 'score' || activeRightTab === 'fault' || activeRightTab === 'encounter';

  const activeColorMap: Record<string, string> = {
    M1: 'var(--c-phos)',
    M2: 'var(--c-phos)',
    M3: 'var(--c-phos)',
    M4: '#38bdf8',
    M5: '#38bdf8',
    M6: 'var(--c-warn)',
    M7: 'var(--c-danger)',
    M8: 'var(--c-danger)',
  };
  const popoverBorderColor = activeBottomModule ? (activeColorMap[activeBottomModule] || 'var(--c-phos)') : 'var(--c-phos)';

  return (
    <div
      data-testid="simulation-monitor"
      data-fsm={fsmState}
      style={{
        height: '100%', display: 'flex', flexDirection: 'column',
        background: 'var(--bg-0)',
        outline: `3px solid ${borderColor}`, outlineOffset: -3,
        boxShadow,
        transition: 'outline-color 0.3s, box-shadow 0.3s',
      }}
    >
      <div style={{ flex: 1, position: 'relative', overflow: 'hidden' }}>


        <SilMapView
          mapRef={externalMapRef}
          followOwnShip={viewMode === 'captain' || viewMode === 'roc'}
          viewMode={viewMode}
          substrate={substrate}
          encRegion={encRegion}
        />

        <SafetyDomainLayer
          mapRef={externalMapRef}
          ownShip={ownShip}
          visible={true}
          threatState={threatState}
        />

        <PlannedRouteLayer mapRef={externalMapRef} waypoints={voyagePlan?.waypoints ?? []} visible={true} />
        <AvoidanceRouteLayer mapRef={externalMapRef} visible={true} />
        <ActualTrackLayer mapRef={externalMapRef} trail={ownShipTrail} visible={true} />

        {(isEngineer || viewMode === 'god') && (
          <>
            <MpcTrajectoryLayer
              mapRef={externalMapRef}
              candidates={sat3?.trajectory_candidates ?? []}
              visible={true}
            />
            {ownShip && sat2 && (
              <IvpRiskGradientLayer
                mapRef={externalMapRef}
                ownShip={ownShip}
                contributions={sat2.ivp_contributions}
                activeBehavior={sat2.active_behavior}
                activeBehaviorWeight={sat2.active_behavior_weight}
              />
            )}
          </>
        )}



        {!ownShip && (
          <div style={{
            position: 'absolute', inset: 0, zIndex: 50,
            display: 'flex', flexDirection: 'column',
            alignItems: 'center', justifyContent: 'center',
            background: 'rgba(7,12,19,0.88)', fontFamily: 'var(--f-mono)',
          }}>
            <div style={{ marginBottom: 16, fontSize: 14, color: 'var(--txt-1)', letterSpacing: '0.16em' }}>
              AWAITING TELEMETRY
            </div>
            <div style={{ fontSize: 10, color: wsConnected ? '#2dd4bf' : '#f87171' }}>
              {wsConnected ? '● WS CONNECTED' : '○ WS DISCONNECTED'} · {wsUrl}
            </div>
          </div>
        )}

        <div
          data-testid="monitor-radar-panel"
          onWheel={handleRadarWheel}
          style={{
            position: 'absolute',
            top: 24,
            left: 24,
            zIndex: 15,
          }}
        >
          <RadarPpiDisplay
            ownShip={ownShip}
            targets={targets}
            relativeMode={viewMode === 'captain'}
            size={460}
            maxRangeNM={radarRangeNM}
            rangeRingsNM={[2, 6, 10]}
          />
          <span
            data-testid="monitor-radar-range"
            style={{
              position: 'absolute',
              left: 78,
              bottom: 74,
              zIndex: 20,
              padding: '2px 6px',
              borderRadius: 4,
              background: 'rgba(5, 15, 10, 0.72)',
              border: '1px solid rgba(16, 185, 129, 0.24)',
              color: 'rgba(226, 232, 240, 0.76)',
              fontFamily: 'var(--f-mono)',
              fontSize: 8,
              letterSpacing: '0',
              pointerEvents: 'none',
            }}
          >
            {radarRangeNM} NM
          </span>
        </div>

        {/* Removed redundant distance scale horizontal line */}


        {/* Unified M4/M5/M7 status info is aggregated inside the right rail drawer */}

        {/* ========================================== */}
        {/* MERGED RIGHT SIDEBAR (CAPTAIN COCKPIT)     */}
        {/* ========================================== */}
        {/* Captain content panel, opened from the unified right rail */}
        <div style={{
          position: 'absolute',
          top: '50%',
          right: 100,
          width: '380px',
          maxHeight: 'calc(100% - 240px)',
          background: 'rgba(13, 19, 31, 0.95)',
          backdropFilter: 'blur(16px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12,
          display: 'flex',
          flexDirection: 'column',
          transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
          opacity: isCaptainPanelActive ? 1 : 0,
          transform: `translateY(-50%) translateX(${isCaptainPanelActive ? '0' : '20px'})`,
          pointerEvents: isCaptainPanelActive ? 'auto' : 'none',
          zIndex: 105,
          boxShadow: isCaptainPanelActive ? '-20px 0 50px rgba(0,0,0,0.5)' : 'none',
          overflow: 'hidden'
        }}>
          {isCaptainPanelActive && (
            <div style={{ display: 'flex', flexDirection: 'column', maxHeight: '100%', overflow: 'hidden', minHeight: 0 }}>
              {/* Header */}
              <div style={{
                padding: '16px 20px', borderBottom: '1px solid var(--line-1)',
                display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                flexShrink: 0
              }}>
                <span style={{
                  fontFamily: 'var(--f-disp)', fontSize: 13, fontWeight: 700,
                  color: 'var(--txt-1)', letterSpacing: '0.15em'
                }}>
                  {RIGHT_TABS.find(t => t.id === activeRightTab)?.label.toUpperCase()}
                </span>
                <button
                  onClick={() => setActiveRightTab(null)}
                  style={{
                    background: 'transparent', border: 'none', color: 'var(--txt-3)',
                    cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center',
                    padding: 4, borderRadius: '50%'
                  }}
                  onMouseEnter={(e) => e.currentTarget.style.color = 'var(--c-phos)'}
                  onMouseLeave={(e) => e.currentTarget.style.color = 'var(--txt-3)'}
                >
                  <LucideChevronRight size={16} />
                </button>
              </div>

              {/* Contents */}
              <div style={{ padding: 20, overflowY: 'auto', flex: 1, minHeight: 0 }}>
                {activeRightTab === 'ship' && (
                  <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
                    <div style={{
                      background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                      padding: '12px 14px', borderRadius: 8,
                    }}>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 8 }}>
                        <div style={{ width: 5, height: 12, background: 'var(--c-phos)', borderRadius: 1 }} />
                        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.08em' }}>航行状态</span>
                      </div>
                      {ownShip ? (
                        <div style={{
                          display: 'grid',
                          gridTemplateColumns: '1fr 1fr',
                          gap: '12px 16px',
                          padding: '4px 2px'
                        }}>
                          {/* Cell 1: HDG */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>船首向 HDG</span>
                            <span data-testid="own-ship-hdg" style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {ownShip.pose?.heading != null ? `${((ownShip.pose.heading * 180 / Math.PI + 360) % 360).toFixed(1)}°` : '—'}
                            </span>
                          </div>

                          {/* Cell 2: RUD */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>舵角 RUD</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {(() => {
                                if (ownShip.controlState?.rudderAngle == null) return '—';
                                const rudderDeg = ownShip.controlState.rudderAngle * 180 / Math.PI;
                                if (Math.abs(rudderDeg) <= 0.1) return '0.0°';
                                return rudderDeg > 0.1 ? `${rudderDeg.toFixed(1)}° R` : `${Math.abs(rudderDeg).toFixed(1)}° L`;
                              })()}
                            </span>
                          </div>

                          {/* Cell 3: SOG */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>对地航速 SOG</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {ownShip.kinematics?.sog != null ? `${(ownShip.kinematics.sog * 1.944).toFixed(1)} kn` : '—'}
                            </span>
                          </div>

                          {/* Cell 4: THR */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>车钟 THR</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {(() => {
                                if (ownShip.controlState?.throttle == null) return '—';
                                const throttle = ownShip.controlState.throttle;
                                if (throttle === 0) return 'STOP';
                                if (throttle > 0) {
                                  if (throttle <= 0.35) return 'AH 1';
                                  if (throttle <= 0.7) return 'AH 2';
                                  return 'AH 3';
                                } else {
                                  if (throttle >= -0.35) return 'AS 1';
                                  if (throttle >= -0.7) return 'AS 2';
                                  return 'AS 3';
                                }
                              })()}
                            </span>
                          </div>
                        </div>
                      ) : (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)' }}>暂无数据</div>
                      )}
                    </div>

                    <div style={{
                      background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                      padding: '12px 14px', borderRadius: 8,
                    }}>
                      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 8 }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                          <div style={{ width: 5, height: 12, background: 'var(--c-phos)', borderRadius: 1 }} />
                          <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.08em' }}>航行计划</span>
                        </div>
                        {planDetails && (
                          <span style={{
                            fontSize: 9,
                            fontFamily: 'var(--f-disp)',
                            fontWeight: 600,
                            letterSpacing: '0.05em',
                            padding: '2px 6px',
                            borderRadius: 4,
                            background: planDetails.source === 'l2_realtime' ? 'rgba(45,212,191,0.15)' : 'rgba(255,255,255,0.06)',
                            border: planDetails.source === 'l2_realtime' ? '1px solid rgba(45,212,191,0.3)' : '1px solid rgba(255,255,255,0.12)',
                            color: planDetails.source === 'l2_realtime' ? 'var(--c-phos)' : 'var(--txt-3)',
                          }}>
                            {planDetails.source === 'l2_realtime' ? 'L2 实时系统' : 'YAML 场景配置'}
                          </span>
                        )}
                      </div>
                      {planDetails ? (
                        <div style={{
                          display: 'grid',
                          gridTemplateColumns: '1fr 1fr',
                          gap: '12px 16px',
                          padding: '4px 2px'
                        }}>
                          {/* Cell 1: WPT */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>计划路点 WPT</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {planDetails.wpt}
                            </span>
                          </div>

                          {/* Cell 2: Dist */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>终点距离 DIST</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {planDetails.dist}
                            </span>
                          </div>

                          {/* Cell 3: ETA */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>剩余航时 ETA</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {planDetails.eta}
                            </span>
                          </div>

                          {/* Cell 4: SPD */}
                          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>计划速度 SPD</span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 20, color: '#fff', fontWeight: 700, lineHeight: 1.1 }}>
                              {planDetails.spd}
                            </span>
                          </div>
                        </div>
                      ) : (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)' }}>暂无数据</div>
                      )}
                    </div>
                  </div>
                )}

                {activeRightTab === 'threat' && (
                  <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
                    {renderThreatRiskTrend(threatRiskHistory)}

                    <div style={{
                      background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                      padding: '12px 14px', borderRadius: 8,
                      display: 'flex', flexDirection: 'column', gap: 12,
                    }}>
                      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                          <div style={{ width: 5, height: 12, background: 'var(--c-phos)', borderRadius: 1 }} />
                          <span style={{ fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.08em' }}>目标列表</span>
                        </div>
                        <span style={{
                          background: 'rgba(45,212,191,0.12)',
                          border: '1px solid rgba(45,212,191,0.28)',
                          color: 'var(--c-phos)',
                          padding: '2px 6px',
                          borderRadius: 4,
                          fontFamily: 'var(--f-mono)',
                          fontSize: 9,
                          fontWeight: 800,
                          letterSpacing: '0.04em',
                        }}>
                          {targetsWithRisk.length} TARGET{targetsWithRisk.length === 1 ? '' : 'S'}
                        </span>
                      </div>
                      {targetsWithRisk.length > 0 ? (
                        renderTargetCards(targetsWithRisk)
                      ) : (
                        <div style={{ color: 'var(--txt-3)', fontSize: 10, fontFamily: 'var(--f-mono)', padding: '4px 2px' }}>
                          暂无监控目标
                        </div>
                      )}
                    </div>
                  </div>
                )}

                {activeRightTab === 'avoid' && (
                  <div style={{ display: 'flex', flexDirection: 'column' }}>
                    <DecisionProcessPanel
                      phaseState={avoidancePhaseState}
                      sat2={sat2}
                      sotifMetrics={sotifMetrics}
                      safetyAlert={safetyAlert}
                      onModuleSelect={setActiveBottomModule}
                    />
                  </div>
                )}
              </div>
            </div>
          )}
        </div>

        {/* ========================================== */}
        {/* RIGHT SIDEBAR (ENGINEER COCKPIT)           */}
        {/* ========================================== */}
        {/* Content Panel on Right side */}
        <div style={{
          position: 'absolute',
          top: '50%',
          right: 100,
          width: '340px',
          maxHeight: 'calc(100% - 240px)',
          background: 'rgba(13, 19, 31, 0.95)',
          backdropFilter: 'blur(16px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12,
          display: 'flex',
          flexDirection: 'column',
          transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
          opacity: isMonitorPanelActive ? 1 : 0,
          transform: `translateY(-50%) translateX(${isMonitorPanelActive ? '0' : '20px'})`,
          pointerEvents: isMonitorPanelActive ? 'auto' : 'none',
          zIndex: 105,
          boxShadow: isMonitorPanelActive ? '-20px 0 50px rgba(0,0,0,0.5)' : 'none',
          overflow: 'hidden'
        }}>
          {isMonitorPanelActive && (
            <div style={{ display: 'flex', flexDirection: 'column', maxHeight: '100%', overflow: 'hidden', minHeight: 0 }}>
              {/* Header */}
              <div style={{
                padding: '16px 20px', borderBottom: '1px solid var(--line-1)',
                display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                flexShrink: 0
              }}>
                <span style={{
                  fontFamily: 'var(--f-disp)', fontSize: 13, fontWeight: 700,
                  color: 'var(--txt-1)', letterSpacing: '0.15em'
                }}>
                  {RIGHT_TABS.find(t => t.id === activeRightTab)?.label.toUpperCase()}
                </span>
		              <button
                  onClick={() => setActiveRightTab(null)}
                  style={{
                    background: 'transparent', border: 'none', color: 'var(--txt-3)',
                    cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center',
                    padding: 4, borderRadius: '50%'
                  }}
                  onMouseEnter={(e) => e.currentTarget.style.color = 'var(--c-phos)'}
                  onMouseLeave={(e) => e.currentTarget.style.color = 'var(--txt-3)'}
                >
                  <LucideChevronRight size={16} />
                </button>
              </div>

              {/* Tab Contents */}
              <div style={{ padding: 20, overflowY: 'auto', flex: 1, minHeight: 0 }}>
                {/* Tab 1: ASDR Ledger */}
                {activeRightTab === 'asdr' && (
                  <div style={{ display: 'flex', flexDirection: 'column', maxHeight: '100%' }}>
                    <div style={{ flex: 1, overflowY: 'auto', maxHeight: '320px', paddingRight: 4 }}>
                      {[...asdrEvents].reverse().slice(0, 30).map((e, i) => (
                        <div key={i} style={{
                          padding: '4px 8px', fontFamily: 'var(--f-mono)', fontSize: 10,
                          color: 'var(--txt-2)', borderBottom: '1px solid rgba(255,255,255,0.03)',
                        }}>
                          <span style={{ color: '#60a5fa' }}>{e.event_type}</span>
                          {e.rule_ref && <span style={{ color: '#a3e635' }}> [{e.rule_ref}]</span>}
                        </div>
                      ))}
                      {asdrEvents.length === 0 && (
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)', textAlign: 'center', padding: '20px 0' }}>
                          暂无 ASDR 事件记录
                        </div>
                      )}
                    </div>
                  </div>
                )}

                {/* Tab 2: Real-time Scoring */}
                {activeRightTab === 'score' && (
                  <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
                    {scoringRow ? (
                      <ScoringGauges visible={true} />
                    ) : (
                      <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)', padding: '20px 0' }}>
                        等待评分系统就绪...
                      </div>
                    )}
                  </div>
                )}

                {/* Tab 3: Fault Injection */}
                {activeRightTab === 'fault' && (
                  <div style={{ display: 'flex', flexDirection: 'column' }}>
                    <FaultInjectPanel inline={true} />
                  </div>
                )}

                {/* Tab 4: Encounter Injection */}
                {activeRightTab === 'encounter' && (
                  <div style={{ display: 'flex', flexDirection: 'column' }}>
                    <EncounterInjectPanel inline={true} />
                  </div>
                )}
              </div>
            </div>
          )}
        </div>

        {/* Vertical Tab Rail on Right side */}
        <div style={{
          position: 'absolute',
          top: '50%',
          right: 20,
          transform: 'translateY(-50%)',
          width: 64,
          height: 'fit-content',
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          paddingTop: 16,
          paddingBottom: 16,
          gap: 8,
          background: 'rgba(10, 15, 24, 0.9)',
          border: '1px solid var(--line-2)',
          borderRadius: 12,
          transition: 'all 0.2s',
          zIndex: 110
        }}>
          {RIGHT_TABS.map((tab) => {
            const active = activeRightTab === tab.id;
            return (
              <button
                key={tab.id}
                title={tab.label}
                data-testid={`right-tab-${tab.id}`}
                onClick={() => setActiveRightTab(active ? null : tab.id)}
                style={{
                  width: 44, height: 44, borderRadius: 8, border: 'none', cursor: 'pointer',
                  background: active ? 'rgba(91,192,190,0.15)' : 'transparent',
                  color: active ? 'var(--c-phos)' : 'var(--txt-3)',
                  display: 'flex', alignItems: 'center', justifyContent: 'center',
                  transition: 'all 0.2s',
                  borderRight: active ? '3px solid var(--c-phos)' : '3px solid transparent',
                  position: 'relative'
                }}
                className="rail-item-right"
              >
                {tab.icon}
                <style>{`
                  .rail-item-right:hover::after {
                    content: attr(title);
                    position: absolute;
                    right: 100%;
                    margin-right: 12px;
                    background: #0d131f;
                    color: var(--txt-1);
                    padding: 6px 12px;
                    border-radius: 4px;
                    font-size: 11px;
                    white-space: nowrap;
                    z-index: 1000;
                    border: 1px solid var(--line-2);
                    pointer-events: none;
                    box-shadow: 0 4px 20px rgba(0,0,0,0.5);
                  }
                `}</style>
              </button>
            );
          })}
        </div>

        {/* ========================================== */}
        {/* BOTTOM PULSE BAR WITH FLOATING POPOVERS   */}
        {/* ========================================== */}
        {activeBottomModule && (
          <div style={{
            position: 'absolute',
            bottom: 68,
            left: `calc(50% - 290px + ${['M1','M2','M3','M4','M5','M6','M7','M8'].indexOf(activeBottomModule) * 72.5}px + 36px)`,
            transform: 'translateX(-50%)',
            width: '280px',
            background: 'rgba(18, 25, 39, 0.96)',
            border: `1px solid ${popoverBorderColor}`,
            borderRadius: 8,
            boxShadow: `0 12px 40px rgba(0,0,0,0.8), 0 0 15px ${popoverBorderColor}26`,
            zIndex: 150,
            display: 'flex',
            flexDirection: 'column',
            padding: 12,
            gap: 8,
            backdropFilter: 'blur(16px)',
          }}>
	            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid rgba(255,255,255,0.06)', paddingBottom: 6 }}>
	              <span style={{ fontSize: 11, fontFamily: 'var(--f-disp)', fontWeight: 700, color: popoverBorderColor, textTransform: 'uppercase', letterSpacing: '0.05em' }}>
	                {BOTTOM_MODULE_TITLES[activeBottomModule as ModuleName] ?? activeBottomModule}
	              </span>
	              <button
	                onClick={() => setActiveBottomModule(null)}
	                style={{ background: 'transparent', border: 'none', color: 'var(--txt-3)', cursor: 'pointer', fontSize: 12 }}
                onMouseEnter={(e) => e.currentTarget.style.color = 'var(--c-danger)'}
                onMouseLeave={(e) => e.currentTarget.style.color = 'var(--txt-3)'}
              >×</button>
	            </div>

	            <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-1)', display: 'flex', flexDirection: 'column', gap: 6 }}>
	              {moduleRealtimeRows[activeBottomModule as ModuleName]?.map((row: ModuleDetailRow) => (
	                <div key={`${activeBottomModule}-${row.label}`} style={{ display: 'flex', justifySelf: 'stretch', justifyContent: 'space-between', gap: 12 }}>
	                  <span className="grid-label">{row.label}</span>
	                  <span style={{ color: row.value === '—' ? 'var(--txt-3)' : popoverBorderColor, fontWeight: 'bold', textAlign: 'right' }}>{row.value}</span>
	                </div>
	              ))}
		            </div>

            {/* Triangular Arrow pointing down to module card */}
            <div style={{
              position: 'absolute',
              bottom: -6,
              left: '50%',
              transform: 'translateX(-50%)',
              width: 0,
              height: 0,
              borderLeft: '6px solid transparent',
              borderRight: '6px solid transparent',
              borderTop: `6px solid ${popoverBorderColor}`
            }} />
          </div>
        )}

        {/* Center Bottom M1-M8 card bar (Width 580px, centered horizontally) */}
        <div style={{
          position: 'absolute',
          bottom: 20,
          left: '50%',
          transform: 'translateX(-50%)',
          display: 'flex',
          background: 'rgba(10, 15, 24, 0.9)',
          border: '1px solid var(--line-2)',
          borderRadius: 12,
          padding: 4,
          gap: 4,
          zIndex: 110,
          backdropFilter: 'blur(12px)',
          width: 580
        }}>
	          {MODULE_NAMES.map((name, i) => {
	            const active = activeBottomModule === name;
	            const p = modulePulses.find(x => Number(x.moduleId) === i + 1);
	            const summary = moduleSummaries[name] ?? '—';
	            const hasRealtimeSummary = summary !== '—';
	            const color = p ? (HEALTH_COLOR[p.state ?? 0] ?? '#444') : hasRealtimeSummary ? (activeColorMap[name] || 'var(--c-phos)') : '#333';
	            const lat = p?.latencyMs;
	            return (
	              <div
                key={name}
                onClick={() => setActiveBottomModule(active ? null : name)}
                style={{
                  flex: 1,
                  height: 38,
                  background: active ? 'rgba(45, 212, 191, 0.12)' : 'transparent',
                  border: active ? '1px solid var(--c-phos)' : '1px solid transparent',
                  borderRadius: 8,
                  display: 'flex',
                  flexDirection: 'column',
                  alignItems: 'center',
                  justifyContent: 'center',
                  cursor: 'pointer',
                  transition: 'all 0.15s ease-out',
                  position: 'relative'
                }}
              >
	                <span style={{ fontSize: 9, fontWeight: 800, color: active ? 'var(--c-phos)' : 'var(--txt-3)', letterSpacing: '0.05em', marginBottom: 2, fontFamily: 'var(--f-mono)' }}>{name}</span>
	                <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
	                  <span style={{ width: 5, height: 5, borderRadius: '50%', background: color }} />
	                  <span style={{ fontSize: 8, color: hasRealtimeSummary || lat != null ? 'var(--txt-2)' : 'var(--txt-3)', fontFamily: 'var(--f-mono)' }}>{hasRealtimeSummary ? summary : lat != null ? `${lat}ms` : '—'}</span>
	                </div>
	              </div>
            );
          })}
        </div>

        <TorModal />

        {/* Chart controls — bottom-right, above the zoom controls */}
        <div style={{ position: 'absolute', bottom: 68, right: 20, zIndex: 20, display: 'flex', alignItems: 'center', gap: 8 }}>
          <MapLayerSwitcher activeLayer={substrate} onLayerChange={setSubstrate} />
        </div>
      </div>

      {isEngineer && <DecisionChainTimingBar pulses={modulePulses} />}

      <div style={{
        height: 48, background: 'var(--bg-1)', borderTop: '1px solid var(--line-2)',
        display: 'flex', alignItems: 'center', padding: '0 24px', gap: 24,
        fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--txt-1)', flexShrink: 0,
      }}>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <button onClick={handlePlay} style={{ background: 'transparent', color: !isPaused ? 'var(--c-phos)' : 'var(--txt-2)', border: 'none', cursor: 'pointer' }}>
            <LucidePlay size={20} />
          </button>
          <button onClick={handlePause} style={{ background: 'transparent', color: isPaused ? 'var(--c-warn)' : 'var(--txt-2)', border: 'none', cursor: 'pointer' }}>
            <LucidePause size={20} />
          </button>
          <button onClick={handleStop} style={{ background: 'transparent', color: 'var(--c-danger)', border: 'none', cursor: 'pointer', marginLeft: 8 }}>
            <LucideSquare size={20} />
          </button>
        </div>

        <div style={{ flex: 1, display: 'flex', alignItems: 'center', gap: 12 }}>
          <div style={{ flex: 1, position: 'relative', display: 'flex', alignItems: 'center' }}>
            <input type="range" min="0" max="600" value={simTimeSec} style={{ flex: 1, accentColor: 'var(--c-phos)' }} readOnly />
            <DecisionEventMarkers events={avoidancePhaseState.events} durationSec={600} />
          </div>
          <span data-testid="sim-clock-text" style={{ color: 'var(--txt-1)' }}>{fmtSimTime(simTimeSec)}</span>
        </div>

        {/* Playback speed selector - Premium Segmented Group */}
        <div style={{
          display: 'flex',
          background: 'var(--bg-2)',
          border: '1px solid var(--line-2)',
          borderRadius: 6,
          padding: 2,
          gap: 2,
        }}>
          {[1, 5, 10].map((r) => {
            const active = simRate === r;
            return (
              <button
                key={r}
                data-testid={`rate-btn-${r}x`}
                onClick={() => handleRateChange(r)}
                onMouseEnter={(e) => {
                  if (!active) {
                    e.currentTarget.style.color = 'var(--txt-1)';
                    e.currentTarget.style.background = 'rgba(255,255,255,0.03)';
                  }
                }}
                onMouseLeave={(e) => {
                  if (!active) {
                    e.currentTarget.style.color = 'var(--txt-3)';
                    e.currentTarget.style.background = 'transparent';
                  }
                }}
                style={{
                  background: active ? 'rgba(45,212,191,0.15)' : 'transparent',
                  color: active ? 'var(--c-phos)' : 'var(--txt-3)',
                  border: 'none',
                  borderRadius: 4,
                  padding: '4px 12px',
                  fontSize: 11,
                  fontFamily: 'var(--f-mono)',
                  fontWeight: active ? 600 : 400,
                  cursor: 'pointer',
                  transition: 'all 0.15s ease',
                  borderBottom: active ? '1px solid var(--c-phos)' : 'none',
                }}
              >
                {r}x
              </button>
            );
          })}
        </div>

        <div style={{ display: 'flex', gap: 16, borderLeft: '1px solid var(--line-2)', paddingLeft: 24 }}>
          <button onClick={toggleAsdr} style={{ background: 'transparent', color: asdrExpanded ? 'var(--c-phos)' : 'var(--txt-2)', border: 'none', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 6 }}>
            <LucideTerminalSquare size={18} /> ASDR
          </button>
          <button onClick={() => setShowFaultModal(true)} style={{ background: 'transparent', color: 'var(--c-danger)', border: 'none', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 6 }}>
            <LucideAlertTriangle size={18} /> FAULT
          </button>
        </div>

      </div>
    </div>
  );
}
