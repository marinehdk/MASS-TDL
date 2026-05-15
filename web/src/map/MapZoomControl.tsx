import React from 'react';
import { LucidePlus, LucideMinus } from 'lucide-react';
import maplibregl from 'maplibre-gl';

interface MapZoomControlProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
}

export const MapZoomControl: React.FC<MapZoomControlProps> = ({ mapRef }) => {
  const handleZoomIn = () => {
    mapRef.current?.zoomIn();
  };

  const handleZoomOut = () => {
    mapRef.current?.zoomOut();
  };

  return (
    <div className="glass-panel" style={{
      position: 'absolute', bottom: 70, right: 20, padding: '4px',
      display: 'flex', gap: 2, borderRadius: 4, zIndex: 100,
      flexDirection: 'column', border: '1px solid var(--line-1)'
    }}>
      <button
        onClick={handleZoomIn}
        style={{
          display: 'flex', alignItems: 'center', justifyContent: 'center', width: 32, height: 32,
          background: 'transparent', border: 'none', color: 'var(--txt-2)',
          borderRadius: 2, cursor: 'pointer', transition: 'all 0.2s'
        }}
        onMouseEnter={(e) => e.currentTarget.style.color = 'var(--c-phos)'}
        onMouseLeave={(e) => e.currentTarget.style.color = 'var(--txt-2)'}
      >
        <LucidePlus size={16} />
      </button>
      <div style={{ height: '1px', background: 'var(--line-1)', margin: '2px 4px' }} />
      <button
        onClick={handleZoomOut}
        style={{
          display: 'flex', alignItems: 'center', justifyContent: 'center', width: 32, height: 32,
          background: 'transparent', border: 'none', color: 'var(--txt-2)',
          borderRadius: 2, cursor: 'pointer', transition: 'all 0.2s'
        }}
        onMouseEnter={(e) => e.currentTarget.style.color = 'var(--c-phos)'}
        onMouseLeave={(e) => e.currentTarget.style.color = 'var(--txt-2)'}
      >
        <LucideMinus size={16} />
      </button>
    </div>
  );
};
