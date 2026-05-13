import React from 'react';
import { LucideLayers, LucideGlobe, LucideMap } from 'lucide-react';

interface MapLayerSwitcherProps {
  activeLayer: 'enc' | 'sat' | 'osm';
  onLayerChange: (layer: 'enc' | 'sat' | 'osm') => void;
}

export const MapLayerSwitcher: React.FC<MapLayerSwitcherProps> = ({ activeLayer, onLayerChange }) => {
  return (
    <div className="glass-panel" style={{
      position: 'absolute', bottom: 20, right: 20, padding: '4px',
      display: 'flex', gap: 4, borderRadius: 8, zIndex: 100
    }}>
      {[
        { id: 'enc', icon: <LucideLayers size={14} />, label: 'ENC' },
        { id: 'sat', icon: <LucideGlobe size={14} />, label: 'SAT' },
        { id: 'osm', icon: <LucideMap size={14} />, label: 'OSM' },
      ].map((layer) => (
        <button
          key={layer.id}
          onClick={() => onLayerChange(layer.id as any)}
          style={{
            display: 'flex', alignItems: 'center', gap: 6, padding: '8px 12px',
            background: activeLayer === layer.id ? 'rgba(91,192,190,0.2)' : 'transparent',
            border: 'none', color: activeLayer === layer.id ? 'var(--c-phos)' : 'var(--txt-3)',
            borderRadius: 6, cursor: 'pointer', fontSize: 11, fontWeight: 700,
            transition: 'all 0.2s'
          }}
        >
          {layer.icon}
          <span>{layer.label}</span>
        </button>
      ))}
    </div>
  );
};
