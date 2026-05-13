// sil-imazu.jsx — Imazu 22 经典会遇几何缩略图
// 每个案例返回 SVG 元素组 (own ship 朝上, target ships 相对位置)

const IMAZU_CASES = [
  // [id, name, rule, ships [{relX, relY, heading, role}]]
  // Own ship at (50, 75) facing up (0°). Targets relative in 100x100 viewport.
  { id: 1, name: 'Head-on', rule: 'R14', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }] },
  { id: 2, name: 'Overtaking', rule: 'R13', ships: [{ x: 50, y: 30, h: 0, role: 'stand-on' }] },
  { id: 3, name: 'Crossing-give-way', rule: 'R15', ships: [{ x: 78, y: 30, h: 240, role: 'give-way' }] },
  { id: 4, name: 'Crossing-stand-on', rule: 'R17', ships: [{ x: 22, y: 30, h: 120, role: 'stand-on' }] },
  { id: 5, name: 'Crossing-fine', rule: 'R15', ships: [{ x: 70, y: 18, h: 220, role: 'give-way' }] },
  { id: 6, name: 'Crossing-broad', rule: 'R15', ships: [{ x: 85, y: 50, h: 260, role: 'give-way' }] },
  { id: 7, name: 'Overtaken', rule: 'R13', ships: [{ x: 50, y: 95, h: 0, role: 'give-way' }] },
  { id: 8, name: 'HO + OT-port', rule: 'R14+R13', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 28, y: 40, h: 0, role: 'stand-on' }] },
  { id: 9, name: 'HO + CR-stbd', rule: 'R14+R15', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 80, y: 35, h: 240, role: 'give-way' }] },
  { id: 10, name: 'HO + CR-port', rule: 'R14+R17', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 20, y: 35, h: 120, role: 'stand-on' }] },
  { id: 11, name: 'CR-stbd + OT', rule: 'R15+R13', ships: [{ x: 78, y: 30, h: 240, role: 'give-way' }, { x: 50, y: 40, h: 0, role: 'stand-on' }] },
  { id: 12, name: 'CR-port + OT', rule: 'R17+R13', ships: [{ x: 22, y: 30, h: 120, role: 'stand-on' }, { x: 50, y: 40, h: 0, role: 'stand-on' }] },
  { id: 13, name: 'HO + CR-stbd + OT', rule: 'R14+R15+R13', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 80, y: 35, h: 240, role: 'give-way' }, { x: 50, y: 45, h: 0, role: 'stand-on' }] },
  { id: 14, name: 'HO + CR-port + OT', rule: 'R14+R17+R13', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 20, y: 35, h: 120, role: 'stand-on' }, { x: 50, y: 45, h: 0, role: 'stand-on' }] },
  { id: 15, name: 'CR-stbd × 2', rule: 'R15×2', ships: [{ x: 78, y: 25, h: 240, role: 'give-way' }, { x: 88, y: 50, h: 260, role: 'give-way' }] },
  { id: 16, name: 'CR-port × 2', rule: 'R17×2', ships: [{ x: 22, y: 25, h: 120, role: 'stand-on' }, { x: 12, y: 50, h: 100, role: 'stand-on' }] },
  { id: 17, name: 'CR-stbd + CR-port', rule: 'R15+R17', ships: [{ x: 78, y: 30, h: 240, role: 'give-way' }, { x: 22, y: 30, h: 120, role: 'stand-on' }] },
  { id: 18, name: 'HO + 2× CR-stbd', rule: 'R14+R15×2', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 75, y: 30, h: 230, role: 'give-way' }, { x: 90, y: 50, h: 255, role: 'give-way' }] },
  { id: 19, name: 'OT + CR-stbd + CR-port', rule: 'R13+R15+R17', ships: [{ x: 50, y: 35, h: 0, role: 'stand-on' }, { x: 78, y: 30, h: 240, role: 'give-way' }, { x: 22, y: 30, h: 120, role: 'stand-on' }] },
  { id: 20, name: 'HO + CR + CR', rule: 'R14+R15+R17', ships: [{ x: 50, y: 18, h: 180, role: 'give-way' }, { x: 78, y: 40, h: 240, role: 'give-way' }, { x: 22, y: 40, h: 120, role: 'stand-on' }] },
  { id: 21, name: 'Dense traffic (4)', rule: '复合', ships: [{ x: 50, y: 22, h: 180, role: 'give-way' }, { x: 75, y: 30, h: 230, role: 'give-way' }, { x: 25, y: 35, h: 130, role: 'stand-on' }, { x: 50, y: 50, h: 0, role: 'stand-on' }] },
  { id: 22, name: 'Restricted waters', rule: 'R9 复合', ships: [{ x: 50, y: 25, h: 180, role: 'give-way' }, { x: 70, y: 40, h: 220, role: 'give-way' }] },
];

const ImazuThumb = ({ caseData, selected, hover, onClick }) => {
  const accent = selected ? COL.warn : hover ? COL.phos : COL.line2;
  return (
    <div onClick={onClick} style={{
      background: selected ? `${COL.warn}10` : COL.bg1,
      border: `1px solid ${accent}`,
      borderLeft: selected ? `2px solid ${COL.warn}` : `1px solid ${accent}`,
      padding: '8px 10px 6px', cursor: 'pointer', transition: 'all 0.12s',
      display: 'flex', flexDirection: 'column', gap: 4,
    }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
        <Disp size={11} color={selected ? COL.warn : COL.t1} weight={600} tracking="0.12em">
          IM{String(caseData.id).padStart(2, '0')}
        </Disp>
        <Mono size={9} color={selected ? COL.warn : COL.t3}>{caseData.rule}</Mono>
      </div>
      <svg viewBox="0 0 100 100" style={{ width: '100%', height: 86, background: '#050810' }}>
        {/* range rings */}
        <circle cx="50" cy="75" r="28" fill="none" stroke={COL.line1} strokeWidth="0.4" strokeDasharray="1.5 2" opacity="0.6" />
        <circle cx="50" cy="75" r="56" fill="none" stroke={COL.line1} strokeWidth="0.4" strokeDasharray="1.5 2" opacity="0.4" />
        {/* heading line */}
        <line x1="50" y1="75" x2="50" y2="20" stroke={COL.phos} strokeWidth="0.5" opacity="0.5" />
        {/* targets */}
        {caseData.ships.map((s, i) => {
          const c = s.role === 'give-way' ? COL.danger : s.role === 'stand-on' ? COL.warn : COL.info;
          const rad = (s.h - 90) * Math.PI / 180;
          const vx = Math.cos(rad) * 12, vy = Math.sin(rad) * 12;
          return (
            <g key={i}>
              <line x1={s.x} y1={s.y} x2={s.x + vx} y2={s.y + vy} stroke={c} strokeWidth="0.7" opacity="0.7" />
              <g transform={`translate(${s.x},${s.y}) rotate(${s.h})`}>
                <path d="M 0 -3 L 2 2 L 0 1 L -2 2 Z" fill="none" stroke={c} strokeWidth="0.8" />
              </g>
            </g>
          );
        })}
        {/* own ship */}
        <g transform="translate(50,75)">
          <path d="M 0 -3.5 L 2.5 2.5 L 0 1 L -2.5 2.5 Z" fill={COL.phos} />
        </g>
      </svg>
      <div style={{
        fontSize: 10, color: selected ? COL.t0 : COL.t2,
        fontFamily: FONT_BODY, lineHeight: 1.1, minHeight: 22,
      }}>{caseData.name}</div>
    </div>
  );
};

Object.assign(window, { IMAZU_CASES, ImazuThumb });
