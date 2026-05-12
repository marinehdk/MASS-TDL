import { useEffect, useRef, useState } from 'react';
import { SidePanel } from './components/SidePanel';
import MapView, { updateOwnShipOnMap, updateTargetsOnMap, type maplibregl } from './components/MapView';
import ChartLegend from './components/ChartLegend';
import { Topbar } from './components/Topbar';
import { useFoxgloveBridge } from './hooks/useFoxgloveBridge';
import { useSilDebug } from './hooks/useSilDebug';
import { DEMO_R14_TARGET, DEMO_OWN_SHIP, makeDemoSilData } from './data/demoTarget';

export default function App() {
  const mapRef = useRef<maplibregl.Map | null>(null);
  const silData = useSilDebug();
  const { ownShip, targets, connected } = useFoxgloveBridge();
  const [selectedScenario, setSelectedScenario] = useState('colreg-rule14-ho-001-v1.0.yaml');

  // Update MapLibre layers when silData (bridge-less) or foxglove_bridge data changes
  useEffect(() => {
    if (connected) {
      updateOwnShipOnMap(mapRef.current, ownShip);
    } else if (silData?.own_ship) {
      updateOwnShipOnMap(mapRef.current, silData.own_ship);
    }
  }, [ownShip, silData?.own_ship, connected]);

  useEffect(() => {
    if (connected) {
      updateTargetsOnMap(mapRef.current, targets);
    } else if (silData?.targets) {
      updateTargetsOnMap(mapRef.current, silData.targets);
    }
  }, [targets, silData?.targets, connected]);

  // Use demo SilDebugData as last resort fallback
  const effectiveSilData = silData ?? (!connected ? makeDemoSilData() : null);

  return (
    <div style={{
      display: 'grid',
      gridTemplateRows: '64px 1fr',
      height: '100vh',
      background: 'var(--bg-0)',
    }}>
      <Topbar data={effectiveSilData} />
      
      <div style={{
        display: 'grid',
        gridTemplateColumns: '1fr 320px',
        overflow: 'hidden',
      }}>
        <div style={{ position: 'relative' }}>
          <MapView mapRef={mapRef} />
          <ChartLegend />
          
          {/* Connection Indicator Overlay */}
          <div style={{
            position: 'absolute',
            top: '12px',
            right: '12px',
            display: 'flex',
            alignItems: 'center',
            gap: '8px',
            padding: '6px 12px',
            background: 'rgba(11,19,32,0.75)',
            backdropFilter: 'blur(8px)',
            border: '1px solid var(--line-1)',
            fontFamily: 'var(--fnt-mono)',
            fontSize: '10px',
            color: connected ? 'var(--color-stbd)' : 'var(--color-port)',
            zIndex: 20,
          }}>
            <div style={{
              width: '6px',
              height: '6px',
              borderHorizontal: '50%',
              background: connected ? 'var(--color-stbd)' : 'var(--color-port)',
              boxShadow: connected ? '0 0 8px var(--color-stbd)' : 'none',
              borderRadius: '50%',
            }} />
            {connected ? 'LIVE BRIDGE: 8765' : 'SIL MOCK MODE'}
          </div>
        </div>
        
        <SidePanel
          data={effectiveSilData}
          scenarioId={selectedScenario}
          onScenarioChange={setSelectedScenario}
        />
      </div>
    </div>
  );
}
