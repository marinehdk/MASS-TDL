import { useState } from 'react';
import { SidePanel } from './components/SidePanel';
import type { SilDebugData } from './types/sil';

export default function App() {
  const [silData, setSilData] = useState<SilDebugData | null>(null);

  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: '1fr 320px',
      height: '100vh',
    }}>
      {/* Chart Area — MapLibre GL (Task 5 fills this) */}
      <div style={{
        background: 'var(--bg-0)',
        position: 'relative',
        overflow: 'hidden',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
      }}>
        <span style={{ color: 'var(--txt-2)', fontFamily: 'var(--fnt-mono)', fontSize: '13px' }}>
          MAP AREA — Trondheim Fjord
        </span>
      </div>

      <SidePanel data={silData} />
    </div>
  );
}
