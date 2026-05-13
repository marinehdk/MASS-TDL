import React from 'react';

interface FooterHotkeyHintsProps {
  screen: 'builder' | 'preflight' | 'bridge' | 'report';
}

const HINTS: Record<string, [string, string][]> = {
  builder:   [['1/2/3', 'Steps A/B/C'], ['CLICK', 'Select Imazu'], ['SAVE', 'Save scenario'], ['→', 'Pre-flight']],
  preflight: [['SPACE', 'Run checks'], ['R', 'Retry failed'], ['→', 'Start Bridge']],
  bridge:    [['SPACE', 'Pause/Resume'], ['T', 'Trigger ToR'], ['F', 'Fault Panel'], ['M', 'MRC'], ['H', 'Handback']],
  report:    [['◀ ▶', 'Scrub'], ['SPACE', 'Play/Pause'], ['E', 'Export ASDR'], ['←', 'Back to Bridge']],
};

export const FooterHotkeyHints: React.FC<FooterHotkeyHintsProps> = ({ screen }) => {
  const hints = HINTS[screen] ?? [];

  return (
    <div
      data-testid="footer-hotkey-hints"
      style={{
        height: 26, background: 'var(--bg-1)', borderTop: '1px solid var(--line-1)',
        display: 'flex', alignItems: 'center', padding: '0 12px', gap: 14, flexShrink: 0,
        zIndex: 40, position: 'relative',
      }}
    >
      {/* WS link status */}
      <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--c-phos)' }}>
        ● WS://m8.local:7820
      </span>
      <div style={{ width: 1, height: 12, background: 'var(--line-2)' }} />
      <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>
        ASDR /var/asdr/run-{'{id}'}.mcap
      </span>

      <div style={{ flex: 1 }} />

      {/* Hotkey hints */}
      {hints.map(([key, desc], i) => (
        <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 5 }}>
          <span style={{
            fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-1)', fontWeight: 600,
            border: '1px solid var(--line-2)', padding: '0 4px', background: 'var(--bg-0)',
          }}>{key}</span>
          <span style={{ fontFamily: 'var(--f-body)', fontSize: 8.5, color: 'var(--txt-3)' }}>{desc}</span>
        </div>
      ))}
    </div>
  );
};
