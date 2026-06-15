import type { RuntimeGate, RuntimeMode, RuntimeSummary } from '../../api/silApi';
import type { GateSSEEvent } from '../../types/gateStream';

export type RuntimeCategory = 'mode' | 'core' | 'plugins' | 'ros' | 'safety' | 'verdict';

const LABELS: Record<RuntimeCategory, string> = {
  mode: '运行模式',
  core: 'TDL 核心容器',
  plugins: '外部插件容器',
  ros: 'ROS2 数据链路',
  safety: '安全边界',
  verdict: '放行结论',
};

const GATE_LABELS: Record<number, string> = {
  1: '系统物理就绪',
  2: '模块脉搏健康',
  3: '场景与环境一致',
  4: '数据源与模型就绪',
  5: '时基严密性验证',
  6: '架构物理隔离',
};

const CATEGORY_GATES: Record<RuntimeCategory, number[]> = {
  mode: [],
  core: [1, 2],
  plugins: [],
  ros: [3],
  safety: [4, 5, 6],
  verdict: [],
};

const INTERNAL_TOPIC_NAMES = [
  '/sil/own_ship_state',
  '/sil/target_vessel_state',
  '/sil/module_pulse',
  '/sil/lifecycle_status',
  '/sil/actuator_cmd',
  '/l3/m5/avoidance_plan',
];

type StatusKind = 'passed' | 'failed' | 'checking' | 'waiting' | 'idle';

function statusLabel(kind: StatusKind) {
  if (kind === 'passed') return '通过';
  if (kind === 'failed') return '失败';
  if (kind === 'checking') return '检查中';
  if (kind === 'waiting') return '等待';
  return '待检查';
}

function statusColors(kind: StatusKind, selected: boolean) {
  if (kind === 'passed') {
    return {
      ring: 'var(--c-stbd)',
      glow: selected ? 'rgba(0, 227, 179, 0.24)' : 'rgba(0, 227, 179, 0.1)',
      badge: 'var(--c-stbd)',
    };
  }
  if (kind === 'failed') {
    return {
      ring: 'var(--c-danger)',
      glow: selected ? 'rgba(248, 81, 73, 0.24)' : 'rgba(248, 81, 73, 0.1)',
      badge: 'var(--c-danger)',
    };
  }
  if (kind === 'checking') {
    return {
      ring: 'var(--c-warn)',
      glow: selected ? 'rgba(240, 183, 47, 0.24)' : 'rgba(240, 183, 47, 0.12)',
      badge: 'var(--c-warn)',
    };
  }
  return {
    ring: selected ? 'var(--c-phos)' : 'var(--line-2)',
    glow: selected ? 'rgba(91, 192, 190, 0.18)' : 'transparent',
    badge: 'var(--txt-3)',
  };
}

function runtimeGateLabel(gate: RuntimeGate) {
  return gate.name;
}

function gateStatus(gateId: number, gates: GateSSEEvent[], streaming: boolean): StatusKind {
  const result = gates.find((gate) => gate.gate_id === gateId);
  if (result?.passed === true) return 'passed';
  if (result?.passed === false) return 'failed';
  if (streaming && gates.length + 1 === gateId) return 'checking';
  return 'waiting';
}

function categoryStatus({
  category,
  gates,
  streaming,
  runtimeSummary,
  preflightVerdict,
  displayMode,
}: {
  category: RuntimeCategory;
  gates: GateSSEEvent[];
  streaming: boolean;
  runtimeSummary?: RuntimeSummary;
  preflightVerdict: 'GO' | 'NO-GO' | null;
  displayMode: RuntimeMode;
}): StatusKind {
  if (category === 'mode') return runtimeSummary?.mode ? 'passed' : 'idle';
  if (category === 'core') {
    if (!runtimeSummary?.core_services.length) return 'idle';
    if (runtimeSummary.core_services.some((service) => service.status !== 'running')) return 'failed';
  }
  if (category === 'plugins') {
    const pluginGate = runtimeSummary?.gates.find((gate) => runtimeGateLabel(gate) === 'single_active_plugin_per_role');
    if (!pluginGate) return 'idle';
    if (!pluginGate.passed) return 'failed';
  }
  if (category === 'ros') {
    if (displayMode === 'internal') return 'passed';
    const plugins = runtimeSummary?.plugin_roles.flatMap((role) => role.plugins) ?? [];
    if (plugins.some((plugin) => plugin.topic_status === 'missing' || plugin.topic_status === 'wrong_type' || plugin.topic_status === 'stale')) {
      return 'failed';
    }
    if (plugins.some((plugin) => plugin.topic_status === 'unchecked')) return 'checking';
    return plugins.length > 0 ? 'passed' : 'waiting';
  }
  if (category === 'verdict') {
    if (preflightVerdict === 'NO-GO' || runtimeSummary?.verdict === 'NO-GO') return 'failed';
    if (preflightVerdict === 'GO' && runtimeSummary?.verdict === 'GO') return 'passed';
    if (streaming) return 'checking';
    return 'waiting';
  }

  const related = CATEGORY_GATES[category].map((gateId) => gateStatus(gateId, gates, streaming));
  if (related.includes('failed')) return 'failed';
  if (related.includes('checking')) return 'checking';
  if (related.length > 0 && related.every((status) => status === 'passed')) return 'passed';
  if (related.length > 0) return 'waiting';
  return 'passed';
}

function categorySummary(category: RuntimeCategory, status: Record<RuntimeCategory, string>, runtimeSummary: RuntimeSummary | undefined, displayMode: RuntimeMode) {
  if (category === 'mode') {
    if (runtimeSummary?.mode === 'internal') return '内测模式';
    if (runtimeSummary?.mode === 'integration') return '集成模式';
  }
  if (category === 'core') {
    const running = runtimeSummary?.core_services.filter((service) => service.status === 'running').length ?? 0;
    const total = runtimeSummary?.core_services.length ?? 0;
    return total > 0 ? `${running}/${total} 核心容器` : `${status.core} 核心容器`;
  }
  if (category === 'plugins') {
    const active = runtimeSummary?.plugin_roles.filter((role) => Boolean(role.active_plugin)).length ?? 0;
    const total = runtimeSummary?.plugin_roles.length ?? 0;
    return total > 0 ? `${active}/${total} 角色容器` : `${status.plugins} 角色容器`;
  }
  if (category === 'ros') {
    if (displayMode === 'internal') return `${INTERNAL_TOPIC_NAMES.length}/${INTERNAL_TOPIC_NAMES.length} 所有话题数量`;
    const plugins = runtimeSummary?.plugin_roles.flatMap((role) => role.plugins) ?? [];
    const topicCount = plugins.reduce((sum, plugin) => sum + Object.keys(plugin.required_topics).length, 0);
    if (topicCount > 0) return `${topicCount}/${topicCount} 所有话题数量`;
  }
  if (category === 'safety') {
    const checked = [4, 5, 6].length;
    return `${checked}/${checked} 所有话题数量`;
  }
  if (category === 'verdict') {
    return preflightVerdictLabel(status.verdict);
  }
  return status[category] ?? 'IDLE';
}

function modeSummary(mode: RuntimeMode) {
  return mode === 'internal' ? '内测模式' : '集成模式';
}

function preflightVerdictLabel(value?: string) {
  return value === 'GO' ? 'GO' : 'FIX';
}

function categoryTitle(category: RuntimeCategory) {
  if (category === 'mode') return '运行模式确认';
  if (category === 'core') return '内部核心容器';
  if (category === 'plugins') return '外部核心容器';
  if (category === 'ros') return 'ROS2数据链路';
  if (category === 'safety') return '安全边界检查';
  if (category === 'verdict') return '仿真检查结论';
  return LABELS[category];
}

function modeNote(mode: RuntimeMode) {
  if (mode === 'integration') return '真实容器，集成除L3外发布版本';
  return '本地MOCK，没有集成其他容器';
}

function topicPriority(topic: string) {
  if (topic === '/fusion/tracked_targets') return 0;
  if (topic === '/route_planning/route_plan' || topic === '/l2/planned_route') return 1;
  if (topic === '/ship/odometry') return 2;
  return 10;
}

function categoryEvidence(category: RuntimeCategory, gates: GateSSEEvent[], runtimeSummary: RuntimeSummary | undefined, displayMode: RuntimeMode) {
  if (category === 'mode') {
    return [modeNote(displayMode)];
  }
  if (category === 'core') {
    return ['4个核心容器检查是否通过'];
  }
  if (category === 'plugins') {
    return ['3个角色容器（航线规划容器，运动控制容器，态势管理容器）检查是否通过'];
  }
  if (category === 'ros') {
    if (displayMode === 'internal') return INTERNAL_TOPIC_NAMES.slice(0, 3);
    const topics = runtimeSummary?.plugin_roles.flatMap((role) =>
      role.plugins.flatMap((plugin) => Object.keys(plugin.required_topics)),
    ) ?? [];
    const mainTopics = [...topics].sort((left, right) => topicPriority(left) - topicPriority(right));
    return mainTopics.length > 0 ? mainTopics.slice(0, 3) : ['暂无话题合同'];
  }
  if (category === 'safety') {
    return [4, 5, 6].map((gateId) => gates.find((item) => item.gate_id === gateId)?.label ?? GATE_LABELS[gateId]);
  }
  if (category === 'verdict') {
    return ['通过检查或者完成修复'];
  }
  return [];
}

export function CheckCategoryNav({
  selected,
  onSelect,
  status,
  gates = [],
  streaming = false,
  focusedGateId,
  onGateSelect,
  preflightVerdict = null,
  runtimeSummary,
  displayMode = 'internal',
}: {
  selected: RuntimeCategory;
  onSelect: (category: RuntimeCategory) => void;
  status: Record<RuntimeCategory, string>;
  gates?: GateSSEEvent[];
  streaming?: boolean;
  focusedGateId?: number | null;
  onGateSelect?: (gateId: number) => void;
  preflightVerdict?: 'GO' | 'NO-GO' | null;
  runtimeSummary?: RuntimeSummary;
  displayMode?: RuntimeMode;
}) {
  return (
    <nav data-testid="check-category-nav" style={{
      display: 'grid',
      gap: 12,
      background: 'rgba(10, 15, 24, 0.95)',
      borderRight: '1px solid var(--line-2)',
      padding: '18px 12px',
      minHeight: '100%',
    }}>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8, padding: '0 4px 10px', borderBottom: '1px solid var(--line-1)' }}>
        <div style={{ width: 4, height: 14, background: 'var(--c-phos)', borderRadius: 2 }} />
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 15, fontWeight: 700, color: 'var(--txt-1)', letterSpacing: '0.18em' }}>
          安全门控总览
        </span>
      </div>
      {(Object.keys(LABELS) as RuntimeCategory[]).map((category, index) => (
        (() => {
          const selectedCategory = selected === category;
          const kind = categoryStatus({ category, gates, streaming, runtimeSummary, preflightVerdict, displayMode });
          const colors = statusColors(kind, selectedCategory);
          const evidence = categoryEvidence(category, gates, runtimeSummary, displayMode);
          return (
            <button
              key={category}
              type="button"
              aria-current={selectedCategory ? 'true' : undefined}
              onClick={() => {
                onSelect(category);
                const gateId = CATEGORY_GATES[category][0];
                if (gateId && onGateSelect) onGateSelect(gateId);
              }}
              style={{
                position: 'relative',
                textAlign: 'left',
                minHeight: 84,
                border: `1px solid ${colors.ring}`,
                background: selectedCategory ? 'rgba(255,255,255,0.03)' : 'rgba(0,0,0,0.15)',
                color: 'var(--txt-1)',
                borderRadius: 8,
                padding: '11px 12px 11px 15px',
                boxShadow: selectedCategory || kind === 'checking' ? `0 0 10px ${colors.glow}` : 'none',
                cursor: 'pointer',
                overflow: 'hidden',
              }}
            >
              {kind !== 'waiting' && kind !== 'idle' && (
                <div style={{
                  position: 'absolute',
                  top: 0,
                  left: 0,
                  bottom: 0,
                  width: 3,
                  background: colors.ring,
                  boxShadow: `0 0 8px ${colors.ring}`,
                }} />
              )}
              <span style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 8 }}>
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, fontWeight: 700, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>
                  {`检查点 0${index + 1}`}
                </span>
                <span style={{
                  fontFamily: 'var(--f-mono)',
                  fontSize: 8.5,
                  fontWeight: 700,
                  color: colors.badge,
                  padding: '2px 5px',
                  borderRadius: 4,
                  background: 'rgba(0,0,0,0.3)',
                  letterSpacing: '0.05em',
                }}>
                  {statusLabel(kind)}
                </span>
              </span>
              <span style={{ display: 'block', fontFamily: 'var(--f-disp)', fontSize: 14, fontWeight: 700, marginTop: 7, color: 'var(--txt-0)' }}>
                {categoryTitle(category)}
              </span>
              <span style={{ display: 'block', color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10, marginTop: 4 }}>
                {category === 'mode' ? modeSummary(displayMode) : categorySummary(category, status, runtimeSummary, displayMode)}
              </span>
              <span style={{ display: 'grid', gap: 3, marginTop: 8 }}>
                {evidence.slice(0, 3).map((item) => (
                  <span
                    key={item}
                    style={{
                      color: item.includes(`Gate 0${focusedGateId}`) ? 'var(--c-phos)' : 'var(--txt-2)',
                      fontFamily: 'var(--f-mono)',
                      fontSize: 9.5,
                      lineHeight: 1.25,
                    }}
                  >
                    {item}
                  </span>
                ))}
              </span>
            </button>
          );
        })()
      ))}
    </nav>
  );
}
