import { SilMapView } from '../map/SilMapView';
import { useFoxgloveLive } from '../hooks/useFoxgloveLive';
import { useTelemetryStore, useControlStore, useUIStore } from '../store';
import { useDeactivateLifecycleMutation } from '../api/silApi';

export function BridgeHMI() {
  useFoxgloveLive();

  const ownShip = useTelemetryStore((s) => s.ownShip);
  const environment = useTelemetryStore((s) => s.environment);
  const modulePulses = useTelemetryStore((s) => s.modulePulses);
  const simRate = useControlStore((s) => s.simRate);
  const isPaused = useControlStore((s) => s.isPaused);
  const setSimRate = useControlStore((s) => s.setSimRate);
  const setPaused = useControlStore((s) => s.setPaused);
  const viewMode = useUIStore((s) => s.viewMode);
  const setViewMode = useUIStore((s) => s.setViewMode);
  const asdrLogExpanded = useUIStore((s) => s.asdrLogExpanded);
  const toggleAsdrLog = useUIStore((s) => s.toggleAsdrLog);
  const [deactivate] = useDeactivateLifecycleMutation();

  const handleStop = async () => {
    await deactivate();
    window.location.hash = '#/report/latest';
  };

  const handlePause = () => setPaused(!isPaused);

  return (
    <div style={{ height: '100vh', display: 'flex', flexDirection: 'column' }}
         data-testid="bridge-hmi">
      {/* Full-screen ENC map */}
      <div style={{ flex: 1, position: 'relative' }}>
        <SilMapView followOwnShip={viewMode === 'captain'} viewMode={viewMode as 'captain' | 'god'} />

        {/* Captain HUD overlay */}
        <div style={{
          position: 'absolute', top: 16, left: 16,
          background: 'rgba(0,0,0,0.7)', padding: 12,
          borderRadius: 8, color: '#e6edf3', fontSize: 13,
          fontFamily: 'monospace', minWidth: 200,
        }}>
          {ownShip ? (
            <>
              <div>SOG: {ownShip.kinematics?.sog?.toFixed(1) ?? '---'} m/s</div>
              <div>COG: {((ownShip.kinematics?.cog ?? 0) * 180 / Math.PI).toFixed(1)}&#176;</div>
              <div>HDG: {((ownShip.pose?.heading ?? 0) * 180 / Math.PI).toFixed(1)}&#176;</div>
              <div>ROT: {((ownShip.kinematics?.rot ?? 0) * 180 / Math.PI).toFixed(2)}&#176;/s</div>
            </>
          ) : (
            <div style={{ color: '#888' }}>Waiting for telemetry...</div>
          )}
          {environment && (
            <div style={{ marginTop: 8, borderTop: '1px solid #444', paddingTop: 8 }}>
              <div>Wind: {environment.wind?.direction?.toFixed(0) ?? '---'}&#176; @ {environment.wind?.speedMps?.toFixed(1) ?? '---'} m/s</div>
              <div>Sea: Beaufort {environment.seaStateBeaufort ?? '---'}</div>
              <div>Vis: {environment.visibilityNm?.toFixed(1) ?? '---'} nm</div>
            </div>
          )}
        </div>

        {/* View mode tabs */}
        <div style={{ position: 'absolute', top: 16, right: 16, display: 'flex', gap: 4 }}>
          {(['captain', 'god', 'roc'] as const).map((mode) => (
            <button key={mode} onClick={() => setViewMode(mode)}
              style={{
                background: viewMode === mode ? '#2dd4bf' : '#333',
                color: '#fff', border: 'none', padding: '4px 12px', borderRadius: 4,
                cursor: 'pointer',
              }}>
              {mode === 'captain' ? 'Captain' : mode === 'god' ? 'God' : 'ROC'}
            </button>
          ))}
        </div>
      </div>

      {/* Bottom bar */}
      <div style={{
        height: 40, background: '#0b1320', display: 'flex',
        alignItems: 'center', padding: '0 16px', gap: 16, fontSize: 12,
        color: '#e6edf3',
      }}>
        {/* Sim time + speed controls */}
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <span data-testid="sim-time">00:00</span>
          <select
            value={simRate}
            onChange={(e) => setSimRate(Number(e.target.value))}
            style={{ background: '#333', color: '#fff', border: 'none', padding: '2px 8px', borderRadius: 4 }}>
            {[0.5, 1, 2, 4, 10].map((r) => (
              <option key={r} value={r}>x{r}</option>
            ))}
          </select>
          <button onClick={handlePause}
            style={{
              background: isPaused ? '#fbbf24' : '#333', color: '#fff',
              border: 'none', padding: '4px 12px', borderRadius: 4, cursor: 'pointer',
            }}>
            {isPaused ? '▶' : '⏸'}
          </button>
        </div>

        {/* Module Pulse bar */}
        <div style={{ display: 'flex', gap: 8, flex: 1, justifyContent: 'center' }}>
          {modulePulses.length > 0 ? modulePulses.map((p, i) => (
            <div key={i} title={p.moduleId?.toString() ?? `M${i+1}`}
              style={{
                width: 16, height: 16, borderRadius: '50%',
                background: p.state === 1 ? '#34d399' : p.state === 2 ? '#fbbf24' : '#f87171',
              }} />
          )) : (
            <span style={{ color: '#666' }}>No module data</span>
          )}
        </div>

        <button onClick={toggleAsdrLog}
          style={{
            background: asdrLogExpanded ? '#2dd4bf' : '#333', color: '#fff',
            border: 'none', padding: '4px 12px', borderRadius: 4, cursor: 'pointer',
          }}>
          ASDR
        </button>
        <button onClick={handleStop}
          style={{
            background: '#f87171', color: '#fff',
            border: 'none', padding: '4px 12px', borderRadius: 4, cursor: 'pointer',
          }}>
          {'⏹'} Stop
        </button>
      </div>
    </div>
  );
}
