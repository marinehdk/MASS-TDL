import { useState } from 'react';
import { SidePanel } from './components/SidePanel';
import MapView from './components/MapView';
import type { SilDebugData } from './types/sil';

export default function App() {
  const [silData, setSilData] = useState<SilDebugData | null>(null);

  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: '1fr 320px',
      height: '100vh',
    }}>
      <MapView />
      <SidePanel data={silData} />
    </div>
  );
}
