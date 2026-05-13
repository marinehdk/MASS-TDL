import React from 'react';

interface FooterHotkeyHintsProps {
  screen: 'builder' | 'preflight' | 'bridge' | 'report';
}

const HINTS: Record<string, [string, string][]> = {
  builder: [['1/2/3', '切换步骤'], ['CLICK', '选择场景'], ['SAVE', '保存'], ['→', '仿真预检']],
  preflight: [['SPACE', '运行检查'], ['R', '重试失败项'], ['→', '启动仿真']],
  bridge: [['SPACE', '暂停/继续'], ['T', '请求接管 (ToR)'], ['F', '故障面板'], ['M', '最小风险 (MRC)'], ['H', '交还控制权']],
  report: [['◀ ▶', '快进/快退'], ['SPACE', '播放/暂停'], ['E', '导出记录 (ASDR)'], ['←', '返回仿真']],
};

export const FooterHotkeyHints: React.FC<FooterHotkeyHintsProps> = ({ screen }) => {
  const hints = HINTS[screen] ?? [];

  return (
    <div
      data-testid="footer-hotkey-hints"
      style={{
        height: 36, background: 'var(--bg-1)', borderTop: '1px solid var(--line-1)',
        display: 'flex', alignItems: 'center', padding: '0 16px', gap: 16, flexShrink: 0,
        zIndex: 40, position: 'relative',
      }}
    >
      {/* WS link status */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
        <span style={{ fontFamily: 'var(--f-body)', fontSize: 12, color: 'var(--txt-2)' }}>
          WebSocket地址
        </span>
        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 600 }}>
          ● WS://m8.local:7820
        </span>
      </div>

      <div style={{ width: 1, height: 16, background: 'var(--line-2)' }} />

      {/* ASDR path */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
        <span style={{ fontFamily: 'var(--f-body)', fontSize: 12, color: 'var(--txt-2)' }}>
          ASDR地址
        </span>
        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--c-phos)', fontWeight: 600 }}>
          ● /var/sil/run-{'{id}'}.mcap
        </span>
      </div>

      <div style={{ flex: 1 }} />

      {/* Hotkey hints */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 20 }}>
        {hints.map(([key, desc], i) => (
          <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <span style={{
              fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-0)', fontWeight: 600,
              border: '1px solid var(--line-2)', borderRadius: 3, padding: '2px 8px', background: 'var(--bg-0)',
              boxShadow: '0 1px 2px rgba(0,0,0,0.2)'
            }}>{key}</span>
            <span style={{ fontFamily: 'var(--f-body)', fontSize: 12, color: 'var(--txt-2)' }}>{desc}</span>
          </div>
        ))}
      </div>
    </div>
  );
};
