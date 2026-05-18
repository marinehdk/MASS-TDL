import { DiffEditor } from '@monaco-editor/react';
import type { GateSSEEvent } from '../../types/gateStream';

interface YamlDiffViewerProps {
  original: string;
  modified: string;
  gate: GateSSEEvent;
}

export function YamlDiffViewer({ original, modified, gate }: YamlDiffViewerProps) {

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: 'var(--bg-0)' }}>
      <div style={{ padding: '8px 16px', borderBottom: '1px solid var(--line-2)', display: 'flex', alignItems: 'center', gap: 8 }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 13, color: 'var(--txt-0)' }}>
          YAML Diff \u00B7 Gate {gate.gate_id}
        </span>
        <span style={{
          fontSize: 10, fontFamily: 'var(--f-mono)', padding: '2px 8px', borderRadius: 3,
          background: gate.passed ? 'rgba(0,227,179,0.15)' : 'rgba(248,81,73,0.15)',
          color: gate.passed ? 'var(--c-stbd)' : 'var(--c-danger)',
        }}>
          {gate.passed ? 'MATCH' : 'MISMATCH'}
        </span>
      </div>
      <div style={{ flex: 1 }}>
        <DiffEditor
          original={original}
          modified={modified}
          language="yaml"
          theme="vs-dark"
          options={{
            readOnly: true,
            renderSideBySide: true,
            minimap: { enabled: false },
            fontSize: 11,
            wordWrap: 'on',
            lineNumbers: 'on',
            scrollBeyondLastLine: false,
          }}
        />
      </div>
      {!gate.passed && (
        <div style={{ padding: '8px 16px', background: 'rgba(248,81,73,0.1)', borderTop: '1px solid var(--c-danger)' }}>
          <span style={{ fontFamily: 'var(--f-body)', fontSize: 11, color: 'var(--c-danger)' }}>
            {'\u26A0'} {gate.rationale}
          </span>
        </div>
      )}
    </div>
  );
}
