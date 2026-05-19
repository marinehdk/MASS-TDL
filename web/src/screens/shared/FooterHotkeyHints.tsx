import React from 'react';
import { useScenarioStore } from '../../store';

interface FooterHotkeyHintsProps {
  screen: 'scenario' | 'check' | 'monitor' | 'evaluator';
}

const HINTS: Record<string, [string, string][]> = {
  scenario:  [['1/2/3', '切换步骤'], ['CLICK', '选择场景'], ['SAVE', '保存'], ['→', '仿真预检']],
  check:     [['SPACE', '运行检查'], ['R', '重试失败项'], ['→', '启动仿真']],
  monitor:   [['SPACE', '暂停/继续'], ['T', '请求接管 (ToR)'], ['F', '故障面板'], ['M', '最小风险 (MRC)'], ['H', '交还控制权']],
  evaluator: [['◀ ▶', '快进/快退'], ['SPACE', '播放/暂停'], ['E', '导出记录 (ASDR)'], ['←', '返回仿真']],
};

export const FooterHotkeyHints: React.FC<FooterHotkeyHintsProps> = ({ screen }) => {
  const hints = HINTS[screen] ?? [];
  const { yamlValid, yamlError, scenarioHash } = useScenarioStore();

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
          ● ws://127.0.0.1:8765
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

      <div style={{ width: 1, height: 16, background: 'var(--line-2)' }} />

      {/* YAML Schema Validation Status */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
        <span style={{ fontFamily: 'var(--f-body)', fontSize: 12, color: 'var(--txt-2)' }}>
          YAML状态
        </span>
        <span style={{
          fontFamily: 'var(--f-mono)', fontSize: 11,
          color: yamlValid ? '#4ade80' : '#f87171', fontWeight: 600,
          display: 'flex', alignItems: 'center', gap: 6
        }}>
          <span>● {yamlValid ? 'Schema 通过' : `Schema 错误: ${yamlError || '格式不符'}`}</span>
          {scenarioHash && (
            <span style={{ color: 'var(--txt-3)', fontSize: 10, fontWeight: 400, marginLeft: 4 }}>
              sha256: {scenarioHash.slice(0, 12)}
            </span>
          )}
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
