export type RuntimeCategory = 'mode' | 'core' | 'plugins' | 'ros' | 'safety' | 'verdict';

const LABELS: Record<RuntimeCategory, string> = {
  mode: '运行模式',
  core: 'TDL 核心容器',
  plugins: '外部插件容器',
  ros: 'ROS2 数据链路',
  safety: '安全边界',
  verdict: '放行结论',
};

export function CheckCategoryNav({
  selected,
  onSelect,
  status,
}: {
  selected: RuntimeCategory;
  onSelect: (category: RuntimeCategory) => void;
  status: Record<RuntimeCategory, string>;
}) {
  return (
    <nav style={{ display: 'grid', gap: 8 }}>
      {(Object.keys(LABELS) as RuntimeCategory[]).map((category, index) => (
        <button
          key={category}
          type="button"
          aria-current={selected === category ? 'true' : undefined}
          onClick={() => onSelect(category)}
          style={{
            textAlign: 'left',
            minHeight: 58,
            border: `1px solid ${selected === category ? 'var(--c-info)' : 'var(--line-1)'}`,
            background: selected === category ? 'var(--bg-2)' : 'var(--bg-1)',
            color: 'var(--txt-1)',
            borderRadius: 5,
            padding: '8px 10px',
          }}
        >
          <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10 }}>0{index + 1}</span>
          <span style={{ display: 'block', fontWeight: 800, marginTop: 4 }}>{LABELS[category]}</span>
          <span style={{ display: 'block', color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10 }}>
            {status[category] ?? 'IDLE'}
          </span>
        </button>
      ))}
    </nav>
  );
}
