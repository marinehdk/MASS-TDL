import React from 'react';

interface DecisionTreeProps {
  ruleChain: string[];
  events: Array<{ event_type: string; rule_ref?: string; verdict?: number }>;
}

export const ColregsDecisionTree: React.FC<DecisionTreeProps> = ({ ruleChain, events }) => {
  if (ruleChain.length === 0 && events.length === 0) {
    return (
      <div data-testid="decision-tree" style={{ color: 'var(--txt-3)', fontSize: 10 }}>
        No rule events captured
      </div>
    );
  }

  const nodes: { indent: number; text: string; color: string }[] = [];
  for (const e of events) {
    if (e.rule_ref) {
      nodes.push({ indent: 0, text: e.rule_ref, color: 'var(--c-info)' });
    }
    if (e.event_type) {
      nodes.push({ indent: 1, text: e.event_type.replace(/_/g, ' '), color: 'var(--c-magenta)' });
    }
    if (e.verdict != null) {
      const vLabel = e.verdict === 1 ? 'PASS' : e.verdict === 2 ? 'RISK' : 'FAIL';
      const vColor = e.verdict === 1 ? 'var(--c-stbd)' : e.verdict === 2 ? 'var(--c-warn)' : 'var(--c-danger)';
      nodes.push({ indent: 2, text: vLabel, color: vColor });
    }
  }

  return (
    <div data-testid="decision-tree">
      <div style={{ color: 'var(--txt-3)', fontSize: 9, letterSpacing: 1, marginBottom: 8 }}>
        COLREGs DECISION TREE
      </div>
      {nodes.map((n, i) => (
        <div key={i} style={{
          marginLeft: n.indent * 16,
          marginBottom: 4,
          fontSize: 9,
          color: n.color,
          fontFamily: 'var(--f-mono)',
        }}>
          {n.indent > 0 && <span style={{ color: 'var(--txt-3)', marginRight: 6 }}>→</span>}
          {n.text}
        </div>
      ))}
      {events.length === 0 && ruleChain.length > 0 && (
        <div style={{ color: 'var(--txt-2)', fontSize: 9, fontFamily: 'var(--f-mono)' }}>
          {ruleChain.join(' → ')}
        </div>
      )}
    </div>
  );
};
