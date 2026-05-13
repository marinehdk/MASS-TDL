// hmi-atoms.jsx — reusable visual atoms for all artboards
// Loaded before all layout/state files. All exports go on window.

const COL = {
  bg0: '#070C13', bg1: '#0B1320', bg2: '#101B2C', bg3: '#16263A',
  line1: '#1B2C44', line2: '#243C58', line3: '#3A5677',
  t0: '#F1F6FB', t1: '#C5D2E0', t2: '#8A9AAD', t3: '#566578',
  phos: '#5BC0BE', stbd: '#3FB950', port: '#F26B6B',
  warn: '#F0B72F', info: '#79C0FF', danger: '#F85149', magenta: '#D070D0',
  d4: '#3FB950', d3: '#79C0FF', d2: '#F0B72F', mrc: '#F85149',
};
const FONT_DISP = "'Saira Condensed','Noto Sans SC',sans-serif";
const FONT_MONO = "'JetBrains Mono',ui-monospace,monospace";
const FONT_BODY = "'Noto Sans SC','Saira Condensed',sans-serif";

// ── Label (small caps display) ─────────────────────────────────────
const Label = ({ children, color = COL.t3, style }) => (
  <div style={{
    fontFamily: FONT_DISP, fontSize: 9.5, letterSpacing: '0.22em',
    textTransform: 'uppercase', color, fontWeight: 500, ...style,
  }}>{children}</div>
);

// ── Mono numeric ───────────────────────────────────────────────────
const Mono = ({ children, size = 13, color = COL.t0, weight = 500, style }) => (
  <span style={{
    fontFamily: FONT_MONO, fontSize: size, color, fontWeight: weight,
    fontVariantNumeric: 'tabular-nums', ...style,
  }}>{children}</span>
);

// ── Disp display text (Saira) ──────────────────────────────────────
const Disp = ({ children, size = 13, color = COL.t0, weight = 500, tracking = '0.08em', style }) => (
  <span style={{
    fontFamily: FONT_DISP, fontSize: size, color, fontWeight: weight,
    letterSpacing: tracking, ...style,
  }}>{children}</span>
);

// ── KPI tile (label / value+unit / sub) ────────────────────────────
const KPI = ({ label, value, unit, sub, color = COL.t0, accent, width }) => (
  <div style={{
    display: 'flex', flexDirection: 'column', gap: 2,
    padding: '6px 12px',
    borderLeft: `1px solid ${COL.line1}`,
    minWidth: width || 88,
  }}>
    <Label>{label}</Label>
    <div style={{ display: 'flex', alignItems: 'baseline', gap: 4 }}>
      <Mono size={18} color={accent || color} weight={600}>{value}</Mono>
      {unit && <Mono size={10} color={COL.t3}>{unit}</Mono>}
    </div>
    {sub && <Mono size={9.5} color={COL.t2}>{sub}</Mono>}
  </div>
);

// ── Autonomy badge ─────────────────────────────────────────────────
const AutoBadge = ({ level = 'D3', label = 'SUPERVISED', state = 'normal' }) => {
  const color = { D4: COL.d4, D3: COL.d3, D2: COL.d2, MRC: COL.mrc }[level] || COL.d3;
  return (
    <div style={{
      display: 'inline-flex', alignItems: 'center', gap: 10,
      padding: '6px 12px',
      border: `1px solid ${color}`,
      background: `${color}14`,
      minWidth: 140,
    }}>
      <div style={{
        width: 8, height: 8, borderRadius: '50%', background: color,
        boxShadow: `0 0 8px ${color}`,
        animation: state === 'critical' ? 'phos-pulse 0.6s infinite' : 'phos-pulse 2.4s infinite',
      }} />
      <Disp size={13} color={COL.t0} weight={600} tracking="0.14em">
        {level} · {label}
      </Disp>
    </div>
  );
};

// ── Section divider ────────────────────────────────────────────────
const Sep = ({ vertical, color = COL.line1 }) => vertical
  ? <div style={{ width: 1, height: '100%', background: color }} />
  : <div style={{ height: 1, width: '100%', background: color }} />;

// ── Panel card ─────────────────────────────────────────────────────
const Panel = ({ title, accent, children, style }) => (
  <div style={{
    background: COL.bg1,
    border: `1px solid ${COL.line1}`,
    borderLeft: accent ? `2px solid ${accent}` : `1px solid ${COL.line1}`,
    padding: 12, ...style,
  }}>
    {title && <Label color={COL.t2} style={{ marginBottom: 8 }}>{title}</Label>}
    {children}
  </div>
);

// ── Schematic chart background (radar + ENC abstraction) ───────────
const ChartBg = ({ width, height, scene = 'transit' }) => {
  // soft ENC-like background with radar sweep
  const cx = width * 0.42, cy = height * 0.72;
  const rings = [80, 160, 240, 320, 400];
  return (
    <svg width={width} height={height} style={{ position: 'absolute', inset: 0 }}>
      <defs>
        <radialGradient id="bg-water" cx="50%" cy="50%" r="80%">
          <stop offset="0%" stopColor="#0d2b4a" />
          <stop offset="60%" stopColor="#0a1e35" />
          <stop offset="100%" stopColor="#070C13" />
        </radialGradient>
        <radialGradient id="sweep" cx="50%" cy="50%" r="50%">
          <stop offset="0%" stopColor={COL.phos} stopOpacity="0.20" />
          <stop offset="40%" stopColor={COL.phos} stopOpacity="0.06" />
          <stop offset="100%" stopColor={COL.phos} stopOpacity="0" />
        </radialGradient>
      </defs>
      <rect width={width} height={height} fill="url(#bg-water)" />
      {/* Coastline abstraction */}
      <path d={`M 0 ${height*0.18} Q ${width*0.25} ${height*0.10}, ${width*0.55} ${height*0.22} T ${width} ${height*0.16} L ${width} 0 L 0 0 Z`}
        fill="#1e2d1e" opacity="0.7" />
      <path d={`M 0 ${height*0.18} Q ${width*0.25} ${height*0.10}, ${width*0.55} ${height*0.22} T ${width} ${height*0.16}`}
        fill="none" stroke="#5a7a3a" strokeWidth="1" opacity="0.5" />
      {/* Depth contours */}
      {[0.32, 0.46, 0.58].map((y, i) => (
        <path key={i}
          d={`M 0 ${height*y} Q ${width*0.3} ${height*(y-0.02)}, ${width*0.6} ${height*y} T ${width} ${height*(y+0.01)}`}
          fill="none" stroke="rgba(100,160,220,0.15)" strokeWidth="0.5" />
      ))}
      {/* Range rings (radar) */}
      {rings.map(r => (
        <circle key={r} cx={cx} cy={cy} r={r}
          fill="none" stroke={COL.line2} strokeWidth="0.5" strokeDasharray="2 4" opacity="0.4" />
      ))}
      {/* Compass cardinal marks */}
      {[0, 90, 180, 270].map(a => {
        const rad = (a - 90) * Math.PI / 180;
        const x1 = cx + Math.cos(rad) * 380, y1 = cy + Math.sin(rad) * 380;
        const x2 = cx + Math.cos(rad) * 410, y2 = cy + Math.sin(rad) * 410;
        const lbl = { 0: 'N', 90: 'E', 180: 'S', 270: 'W' }[a];
        return (
          <g key={a}>
            <line x1={x1} y1={y1} x2={x2} y2={y2} stroke={COL.t3} strokeWidth="1" />
            <text x={x2 + (a === 90 ? 4 : a === 270 ? -14 : -4)} y={y2 + (a === 0 ? -4 : a === 180 ? 12 : 4)}
              fill={COL.t3} fontSize="10" fontFamily={FONT_MONO}>{lbl}</text>
          </g>
        );
      })}
      {/* Radar sweep fan */}
      <g style={{ transformOrigin: `${cx}px ${cy}px`, animation: 'radar-sweep 6s linear infinite' }}>
        <path d={`M ${cx} ${cy} L ${cx + 400} ${cy} A 400 400 0 0 0 ${cx + 283} ${cy - 283} Z`}
          fill="url(#sweep)" />
      </g>
    </svg>
  );
};

// ── Own ship triangle ──────────────────────────────────────────────
const OwnShip = ({ x, y, heading = 0, scale = 1 }) => (
  <g transform={`translate(${x},${y}) rotate(${heading})`}>
    <path d="M 0 -14 L 7 8 L 0 4 L -7 8 Z"
      fill={COL.phos} stroke={COL.phos} strokeWidth="1" opacity="0.95"
      transform={`scale(${scale})`} />
    {/* HDG line */}
    <line x1="0" y1="0" x2="0" y2={-110 * scale} stroke={COL.phos} strokeWidth="1" opacity="0.7" />
  </g>
);

// ── Target ship ────────────────────────────────────────────────────
const Target = ({ x, y, heading = 180, role = 'safe', label, cpa, tcpa, showAPF = false }) => {
  const roleColor = role === 'give-way' ? COL.danger : role === 'stand-on' ? COL.warn : COL.info;
  const apfColor = role === 'give-way' ? 'rgba(242,107,107,0.22)' : role === 'stand-on' ? 'rgba(240,183,47,0.18)' : 'rgba(121,192,255,0.12)';
  return (
    <g>
      {showAPF && (
        <ellipse cx={x} cy={y} rx={80} ry={56}
          fill={apfColor}
          transform={`rotate(${heading - 90} ${x} ${y})`} />
      )}
      <g transform={`translate(${x},${y}) rotate(${heading})`}>
        <path d="M 0 -10 L 6 7 L 0 3 L -6 7 Z"
          fill="none" stroke={roleColor} strokeWidth="1.5" />
        <line x1="0" y1="0" x2="0" y2="-50" stroke={roleColor} strokeWidth="1" opacity="0.6" />
      </g>
      {label && (
        <g transform={`translate(${x + 14},${y - 4})`}>
          <text fontFamily={FONT_DISP} fontSize="10" letterSpacing="0.1em" fill={COL.t1}>{label}</text>
          {cpa && (
            <text fontFamily={FONT_MONO} fontSize="9" fill={COL.t2} y="12">
              CPA {cpa}nm · TCPA {tcpa}
            </text>
          )}
        </g>
      )}
    </g>
  );
};

// ── Ghost ship predicted track ─────────────────────────────────────
const GhostTrack = ({ pts, color = COL.magenta }) => {
  const d = pts.map((p, i) => `${i === 0 ? 'M' : 'L'} ${p[0]} ${p[1]}`).join(' ');
  return (
    <g>
      <path d={d} fill="none" stroke={color} strokeWidth="1.5" strokeDasharray="6 4" opacity="0.8" />
      {pts.slice(1).map((p, i) => (
        <g key={i}>
          <circle cx={p[0]} cy={p[1]} r="3" fill={color} opacity="0.6" />
          <text x={p[0] + 6} y={p[1] - 4} fontFamily={FONT_MONO} fontSize="8" fill={color}>
            T+{[15, 30, 60][i]}s
          </text>
        </g>
      ))}
    </g>
  );
};

Object.assign(window, {
  COL, FONT_DISP, FONT_MONO, FONT_BODY,
  Label, Mono, Disp, KPI, AutoBadge, Sep, Panel,
  ChartBg, OwnShip, Target, GhostTrack,
});
