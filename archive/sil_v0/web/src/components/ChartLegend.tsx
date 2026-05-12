/**
 * Floating chart legend overlay for the ENC MapLibre view.
 * Aligned with HMI Design Spec v1.0 §8 (Night theme tokens, no rounded corners).
 *
 * Mirrors the enc-depth stop palette in MapView so the swatches stay in lockstep
 * if the depth ramp ever changes.
 */

const DEPTH_STOPS: ReadonlyArray<{ depth: number; color: string }> = [
  { depth: 0, color: '#5a89b8' },
  { depth: 1, color: '#3e6da0' },
  { depth: 5, color: '#23548b' },
  { depth: 10, color: '#143f73' },
  { depth: 20, color: '#0d2f5c' },
  { depth: 50, color: '#082147' },
  { depth: 100, color: '#04152e' },
];

const labelStyle: React.CSSProperties = {
  fontFamily: 'var(--fnt-disp)',
  fontSize: '9px',
  letterSpacing: '0.18em',
  color: 'var(--txt-3)',
  textTransform: 'uppercase',
  fontWeight: 500,
};

const rowStyle: React.CSSProperties = {
  display: 'flex',
  alignItems: 'center',
  gap: '6px',
  fontFamily: 'var(--fnt-mono)',
  fontSize: '10px',
  color: 'var(--txt-1)',
  lineHeight: 1.4,
};

export default function ChartLegend() {
  return (
    <div
      style={{
        position: 'absolute',
        bottom: '44px',
        left: '12px',
        padding: '8px 10px 10px',
        background: 'rgba(11,19,32,0.78)',
        backdropFilter: 'blur(6px)',
        border: '1px solid var(--line-1)',
        borderRadius: 'var(--radius-none)',
        minWidth: '186px',
        zIndex: 11,
      }}
    >
      <div style={{ ...labelStyle, marginBottom: '6px' }}>Chart Legend · ENC</div>

      {/* Depth ramp */}
      <div style={{ marginBottom: '8px' }}>
        <div style={{ display: 'flex', height: '10px' }}>
          {DEPTH_STOPS.map((s) => (
            <div key={s.depth} style={{ flex: 1, background: s.color }} />
          ))}
        </div>
        <div
          style={{
            display: 'flex',
            justifyContent: 'space-between',
            marginTop: '2px',
            fontFamily: 'var(--fnt-mono)',
            fontSize: '8px',
            color: 'var(--txt-3)',
          }}
        >
          {DEPTH_STOPS.map((s) => (
            <span key={s.depth} style={{ flex: 1, textAlign: 'center' }}>
              {s.depth}
            </span>
          ))}
        </div>
        <div
          style={{
            fontFamily: 'var(--fnt-mono)',
            fontSize: '8px',
            color: 'var(--txt-3)',
            textAlign: 'center',
            marginTop: '1px',
          }}
        >
          depth · m (minimumsdybde)
        </div>
      </div>

      {/* Symbol key */}
      <div style={{ display: 'grid', rowGap: '4px' }}>
        <div style={rowStyle}>
          <span style={{ width: 14, height: 8, background: '#2d3520', border: '1px solid #5a7a3a' }} />
          <span>Land · coastline (landareal / kystkontur)</span>
        </div>
        <div style={rowStyle}>
          <svg width="14" height="8" viewBox="0 0 14 8">
            <line x1="0" y1="4" x2="14" y2="4" stroke="rgba(90,160,210,0.55)" strokeWidth="0.8" />
          </svg>
          <span>Depth contour (dybdekurve)</span>
        </div>
        <div style={rowStyle}>
          <svg width="14" height="8" viewBox="0 0 14 8">
            <circle cx="7" cy="4" r="2.5" fill="#F26B6B" fillOpacity="0.55" />
          </svg>
          <span>Shoal · rock (grunne / skjer)</span>
        </div>
        <div style={rowStyle}>
          <span
            style={{
              width: 14,
              height: 8,
              background: '#F85149',
              opacity: 0.25,
              border: '1px dashed rgba(248,81,73,0.55)',
            }}
          />
          <span>Danger area (fareomrade)</span>
        </div>
        <div style={rowStyle}>
          <svg width="14" height="10" viewBox="0 0 14 10">
            <polygon points="7,1 12,9 2,9" fill="#5BC0BE" stroke="#5BC0BE" strokeWidth="0.6" />
          </svg>
          <span>Own ship (FCB) · phosphor</span>
        </div>
        <div style={rowStyle}>
          <svg width="14" height="10" viewBox="0 0 14 10">
            <polygon points="7,1 12,9 2,9" fill="#F26B6B" stroke="#F26B6B" strokeWidth="0.6" />
          </svg>
          <span>Target · COLREGs role-coded</span>
        </div>
        <div style={rowStyle}>
          <svg width="14" height="10" viewBox="0 0 14 10">
            <circle cx="7" cy="5" r="3.5" fill="none" stroke="#F26B6B" strokeWidth="0.7" strokeDasharray="2 1.5" />
          </svg>
          <span>CPA ring · closest threat</span>
        </div>
        <div style={rowStyle}>
          <svg width="14" height="8" viewBox="0 0 14 8">
            <line x1="0" y1="4" x2="14" y2="4" stroke="#5BC0BE" strokeWidth="1.2" strokeDasharray="3 1.5" />
          </svg>
          <span>COG vector · 60 s prediction</span>
        </div>
      </div>
    </div>
  );
}
