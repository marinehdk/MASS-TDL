import { useEffect, useRef, useState } from 'react';
import { SidePanel } from './components/SidePanel';
import MapView, { updateOwnShipOnMap, updateTargetsOnMap, type maplibregl } from './components/MapView';
import ChartLegend from './components/ChartLegend';
import { useFoxgloveBridge } from './hooks/useFoxgloveBridge';
import { useSilDebug } from './hooks/useSilDebug';
import { DEMO_R14_TARGET, DEMO_OWN_SHIP, makeDemoSilData } from './data/demoTarget';

export default function App() {
  const mapRef = useRef<maplibregl.Map | null>(null);
  const silData = useSilDebug();
  const { ownShip, targets, connected } = useFoxgloveBridge();
  const [selectedScenario, setSelectedScenario] = useState('colreg-rule14-ho-001-seed42-v1.0.yaml');

  // DEMO-1 fallback: when foxglove_bridge is not connected, show static R14 encounter
  useEffect(() => {
    if (!connected && mapRef.current) {
      const timer = setTimeout(() => {
        updateOwnShipOnMap(mapRef.current, DEMO_OWN_SHIP);
        updateTargetsOnMap(mapRef.current, [DEMO_R14_TARGET]);
      }, 500);
      return () => clearTimeout(timer);
    }
  }, [connected]);

  // Update MapLibre layers when foxglove_bridge data changes
  useEffect(() => {
    updateOwnShipOnMap(mapRef.current, ownShip);
  }, [ownShip]);

  useEffect(() => {
    updateTargetsOnMap(mapRef.current, targets);
  }, [targets]);

  // Use demo SilDebugData when /ws/sil_debug is not connected
  const effectiveSilData = silData ?? (!connected ? makeDemoSilData() : null);

  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: '1fr 320px',
      height: '100vh',
    }}>
      <div style={{ position: 'relative' }}>
        <MapView mapRef={mapRef} />
        <ChartLegend />
        {/* foxglove_bridge connection indicator */}
        <div style={{
          position: 'absolute',
          top: '8px',
          right: '8px',
          display: 'flex',
          alignItems: 'center',
          gap: '6px',
          padding: '4px 10px',
          background: 'rgba(11,19,32,0.75)',
          backdropFilter: 'blur(4px)',
          border: '1px solid var(--line-1)',
          fontFamily: 'var(--fnt-mono)',
          fontSize: '10px',
          color: connected ? 'var(--color-stbd)' : 'var(--color-port)',
          borderRadius: 'var(--radius-none)',
          zIndex: 20,
        }}>
          <div style={{
            width: '6px',
            height: '6px',
            borderRadius: '50%',
            background: connected ? 'var(--color-stbd)' : 'var(--color-port)',
            boxShadow: connected ? '0 0 6px var(--color-stbd)' : 'none',
          }} />
          {connected ? 'foxglove 8765' : 'fs disconnected'}
        </div>
      </div>
      <SidePanel
        data={effectiveSilData}
        scenarioId={selectedScenario}
        onScenarioChange={setSelectedScenario}
      />
    </div>
  );
}
