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
      display: 'flex', gap: 4, borderRadius: 4, zIndex: 100,
      height: 38, alignItems: 'center', border: '1px solid var(--line-1)'
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
            display: 'flex', alignItems: 'center', gap: 6, padding: '0 12px',
            height: 30,
            background: activeLayer === layer.id ? 'rgba(91,192,190,0.15)' : 'transparent',
            border: 'none', color: activeLayer === layer.id ? 'var(--c-phos)' : 'var(--txt-2)',
            borderRadius: 2, cursor: 'pointer', fontSize: 11, fontWeight: 700,
            fontFamily: 'var(--f-disp)', letterSpacing: '0.05em',
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
