import { useState } from 'react';

// ── IMAZU-22 case definitions ──────────────────────────────────────────────────
const IMAZU_CASES = [
  { id: 1, name: 'HO-GW', angle: 0 },
  { id: 2, name: 'HO-GP', angle: 5 },
  { id: 3, name: 'GO-GW', angle: 10 },
  { id: 4, name: 'GO-GP', angle: 15 },
  { id: 5, name: 'CR-GW', angle: 22.5 },
  { id: 6, name: 'CR-GP', angle: 45 },
  { id: 7, name: 'CR-LP', angle: 67.5 },
  { id: 8, name: 'CR-SW', angle: 90 },
  { id: 9, name: 'CR-IP', angle: 112.5 },
  { id: 10, name: 'CR-RW', angle: 135 },
  { id: 11, name: 'CR-RP', angle: 157.5 },
  { id: 12, name: 'OV-TA', angle: 180 },
  { id: 13, name: 'OV-SB', angle: 0 },
  { id: 14, name: 'HO-OT', angle: 2.5 },
  { id: 15, name: 'GO-OT', angle: 20 },
  { id: 16, name: 'CR-OT', angle: 60 },
  { id: 17, name: 'NO-RK', angle: 30 },
  { id: 18, name: 'NO-NR', angle: 120 },
  { id: 19, name: 'WA-CB', angle: 270 },
  { id: 20, name: 'WA-NB', angle: 80 },
  { id: 21, name: 'ML-CP', angle: 150 },
  { id: 22, name: 'NT-AP', angle: 200 },
];

const FAULT_ROWS = [
  { id: 'AIS_DROPOUT', at: 60, dur: 30, checked: false },
  { id: 'RADAR_SPIKE', at: 120, dur: 10, checked: false },
  { id: 'DIST_STEP', at: 200, dur: 60, checked: false },
  { id: 'SENSOR_FREEZE', at: 300, dur: 20, checked: false },
];

const PASS_ROWS = [
  { label: 'CPA min', target: '0.50nm' },
  { label: 'COLREGs', target: '100%' },
  { label: 'Path deviation', target: '≤2nm' },
  { label: 'ToR response', target: '≤60s' },
  { label: 'M5 solve p95', target: '≤500ms' },
  { label: 'ASDR integrity', target: '100%' },
];

// ── Mini SVG for IMAZU case card ────────────────────────────────────────────────
function MiniCaseSVG({ angle }: { angle: number }) {
  const rad = (angle * Math.PI) / 180;
  const tx = 12 + 8 * Math.cos(rad);
  const ty = 12 - 8 * Math.sin(rad);
  return (
    <svg width={24} height={24} viewBox="0 0 24 24" style={{ flexShrink: 0 }}>
      <polygon points="12,6 10,14 14,14" fill="#f87171" />
      <polygon points={`${tx},${ty - 2} ${tx - 2},${ty + 3} ${tx + 2},${ty + 3}`} fill="#60a5fa" />
    </svg>
  );
}

// ── Shared input style ──────────────────────────────────────────────────────────
const inputStyle: React.CSSProperties = {
  background: 'rgba(0,0,0,0.2)',
  border: '1px solid var(--line-1)',
  color: 'var(--txt-1)',
  fontFamily: 'var(--f-mono)',
  fontSize: 11,
  padding: '4px 6px',
  borderRadius: 4,
  outline: 'none',
  width: '100%',
};

const fieldRowStyle: React.CSSProperties = {
  display: 'flex',
  alignItems: 'center',
  gap: 8,
  marginBottom: 6,
};

const labelStyle: React.CSSProperties = {
  fontFamily: 'var(--f-disp)',
  fontSize: 10,
  color: 'var(--txt-3)',
  letterSpacing: 1,
  minWidth: 80,
  flexShrink: 0,
};

const sectionHeaderStyle: React.CSSProperties = {
  fontFamily: 'var(--f-disp)',
  fontSize: 10,
  color: 'var(--c-phos)',
  letterSpacing: 2,
  borderBottom: '1px solid var(--line-1)',
  paddingBottom: 4,
  marginBottom: 8,
  marginTop: 12,
};

// ── Tab definitions ─────────────────────────────────────────────────────────────
const TABS = [
  { key: 'encounter' as const, label: 'ENC', icon: '⊕', title: 'Encounter' },
  { key: 'environment' as const, label: 'ENV', icon: '⛅', title: 'Environment' },
  { key: 'run' as const, label: 'RUN', icon: '▶', title: 'Run' },
  { key: 'summary' as const, label: 'SUM', icon: '☰', title: 'Summary' },
] as const;

type TabKey = typeof TABS[number]['key'];

export interface BuilderRightRailProps {
  previewData: any;
  onRun: () => void;
  onSave: () => void;
  onValidate: () => void;
}

export function BuilderRightRail({ previewData, onRun, onSave, onValidate }: BuilderRightRailProps) {
  const [activeTab, setActiveTab] = useState<TabKey | null>(null);
  const [selectedCase, setSelectedCase] = useState<number>(1);

  // Encounter params
  const [dcpaTarget, setDcpaTarget] = useState(0.5);
  const [tcpaTarget, setTcpaTarget] = useState(10.0);
  const [tsRelBearing, setTsRelBearing] = useState(0);
  const [tsDistance, setTsDistance] = useState(7.0);
  const [tsSog, setTsSog] = useState(10.0);
  const [tsCog, setTsCog] = useState(180);

  // Environment params
  const [beaufort, setBeaufort] = useState(0);
  const [windDir, setWindDir] = useState(0);
  const [waveHs, setWaveHs] = useState(0);
  const [waveDir, setWaveDir] = useState(0);
  const [currentSpeed, setCurrentSpeed] = useState(0);
  const [currentDir, setCurrentDir] = useState(0);
  const [visibility, setVisibility] = useState(10);
  const [fog, setFog] = useState(false);
  const [rain, setRain] = useState(false);
  const [gnssNoise, setGnssNoise] = useState(0);
  const [aisLoss, setAisLoss] = useState(0);
  const [radarRange, setRadarRange] = useState(12);

  // Ship/ODD params
  const [hullType, setHullType] = useState<'FCB' | 'ASV'>('FCB');
  const [displacement, setDisplacement] = useState(450);
  const [initHeading, setInitHeading] = useState(0);
  const [initSog, setInitSog] = useState(10);
  const [maxRudder, setMaxRudder] = useState(35);
  const [maxRot, setMaxRot] = useState(2.0);

  // Run params
  const [duration, setDuration] = useState(700);
  const [simRate, setSimRate] = useState(1.0);
  const [randomSeed, setRandomSeed] = useState(42);
  const [m2Hz, setM2Hz] = useState(10);
  const [m5Hz, setM5Hz] = useState(4);
  const [autonomyLevel, setAutonomyLevel] = useState<'D2' | 'D3' | 'D4'>('D3');
  const [operatorRole, setOperatorRole] = useState<'Observer' | 'Supervisor' | 'Commander'>('Supervisor');
  const [colregAvoidW, setColregAvoidW] = useState(80);
  const [missionEtaW, setMissionEtaW] = useState(50);
  const [cpaMinThreshold, setCpaMinThreshold] = useState(0.5);
  const [tcpaTrigger, setTcpaTrigger] = useState(10.0);

  const expanded = activeTab !== null;
  const railWidth = expanded ? 320 : 48;

  return (
    <div
      data-testid="builder-right-rail"
      style={{
        width: railWidth,
        minWidth: railWidth,
        background: '#0a0f18',
        borderLeft: '1px solid var(--line-2)',
        display: 'flex',
        transition: 'width 200ms cubic-bezier(0.4, 0, 0.2, 1), min-width 200ms cubic-bezier(0.4, 0, 0.2, 1)',
        overflow: 'hidden',
        flexShrink: 0,
      }}
    >
      {/* ── Left strip: tab icons ──── */}
      <div style={{
        width: 48,
        minWidth: 48,
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        paddingTop: 12,
        flexShrink: 0,
      }}>
        {TABS.map((tab) => (
          <button
            key={tab.key}
            title={tab.title}
            onClick={() => setActiveTab(activeTab === tab.key ? null : tab.key)}
            style={{
              width: 40,
              height: 40,
              display: 'flex',
              flexDirection: 'column',
              alignItems: 'center',
              justifyContent: 'center',
              gap: 2,
              background: activeTab === tab.key ? 'rgba(91,192,190,0.15)' : 'transparent',
              border: 'none',
              borderBottom: activeTab === tab.key ? '2px solid var(--c-phos)' : '2px solid transparent',
              color: activeTab === tab.key ? 'var(--c-phos)' : 'var(--txt-3)',
              cursor: 'pointer',
              padding: 0,
              marginBottom: 4,
              transition: 'all 0.15s',
            }}
          >
            <span style={{ fontSize: 16, lineHeight: 1 }}>{tab.icon}</span>
            <span style={{ fontSize: 8, fontWeight: 700, fontFamily: 'var(--f-mono)', letterSpacing: 1 }}>{tab.label}</span>
          </button>
        ))}

        {/* Collapse button — only visible when expanded */}
        {expanded && (
          <button
            title="Collapse"
            onClick={() => setActiveTab(null)}
            style={{
              marginTop: 'auto',
              marginBottom: 12,
              width: 32,
              height: 32,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              background: 'transparent',
              border: '1px solid var(--line-1)',
              borderRadius: 4,
              color: 'var(--txt-3)',
              cursor: 'pointer',
              fontSize: 14,
            }}
          >
            {'◀'}
          </button>
        )}
      </div>

      {/* ── Content panel ──────────── */}
      {expanded && activeTab && (
        <div style={{
          flex: 1,
          overflowY: 'auto',
          padding: '12px 12px 24px',
          borderLeft: '1px solid var(--line-1)',
        }}>
          {/* ── Tab A: Encounter ──── */}
          {activeTab === 'encounter' && (
            <>
              <div style={sectionHeaderStyle}>IMAZU-22 CASES</div>
              <div style={{
                display: 'grid',
                gridTemplateColumns: 'repeat(4, 1fr)',
                gap: 4,
                marginBottom: 12,
              }}>
                {IMAZU_CASES.map((c) => (
                  <button
                    key={c.id}
                    onClick={() => setSelectedCase(c.id)}
                    style={{
                      display: 'flex',
                      flexDirection: 'column',
                      alignItems: 'center',
                      gap: 2,
                      padding: '4px 2px',
                      background: selectedCase === c.id ? 'rgba(91,192,190,0.15)' : 'rgba(0,0,0,0.15)',
                      border: selectedCase === c.id ? '1px solid var(--c-phos)' : '1px solid var(--line-1)',
                      borderRadius: 4,
                      cursor: 'pointer',
                      color: selectedCase === c.id ? 'var(--c-phos)' : 'var(--txt-2)',
                    }}
                  >
                    <MiniCaseSVG angle={c.angle} />
                    <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, fontWeight: 700 }}>{c.id}</span>
                    <span style={{ fontFamily: 'var(--f-mono)', fontSize: 7, opacity: 0.6 }}>{c.name}</span>
                  </button>
                ))}
              </div>

              <div style={sectionHeaderStyle}>ENCOUNTER PARAMS</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>DCPA Target</span>
                <input type="number" step={0.01} value={dcpaTarget} onChange={(e) => setDcpaTarget(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>nm</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>TCPA Target</span>
                <input type="number" step={0.1} value={tcpaTarget} onChange={(e) => setTcpaTarget(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>min</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>TS Rel Brg</span>
                <input type="number" value={tsRelBearing} onChange={(e) => setTsRelBearing(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>deg</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>TS Distance</span>
                <input type="number" step={0.1} value={tsDistance} onChange={(e) => setTsDistance(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>nm</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>TS SOG</span>
                <input type="number" step={0.1} value={tsSog} onChange={(e) => setTsSog(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>kn</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>TS COG</span>
                <input type="number" value={tsCog} onChange={(e) => setTsCog(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>deg</span>
              </div>
            </>
          )}

          {/* ── Tab B: Environment ──── */}
          {activeTab === 'environment' && (
            <>
              <div style={sectionHeaderStyle}>WIND</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Beaufort</span>
                <select value={beaufort} onChange={(e) => setBeaufort(Number(e.target.value))} style={inputStyle}>
                  {Array.from({ length: 13 }, (_, i) => (
                    <option key={i} value={i}>{i}</option>
                  ))}
                </select>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Wind Dir</span>
                <input type="number" value={windDir} onChange={(e) => setWindDir(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>deg</span>
              </div>

              <div style={sectionHeaderStyle}>WAVE</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Hs</span>
                <input type="number" step={0.1} value={waveHs} onChange={(e) => setWaveHs(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>m</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Wave Dir</span>
                <input type="number" value={waveDir} onChange={(e) => setWaveDir(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>deg</span>
              </div>

              <div style={sectionHeaderStyle}>CURRENT</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Speed</span>
                <input type="number" step={0.1} value={currentSpeed} onChange={(e) => setCurrentSpeed(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>kn</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Direction</span>
                <input type="number" value={currentDir} onChange={(e) => setCurrentDir(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>deg</span>
              </div>

              <div style={sectionHeaderStyle}>VISIBILITY</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Visibility</span>
                <input type="number" step={0.1} value={visibility} onChange={(e) => setVisibility(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>nm</span>
              </div>
              <div style={{ display: 'flex', gap: 8, marginBottom: 6 }}>
                <label style={{ display: 'flex', alignItems: 'center', gap: 4, color: 'var(--txt-2)', fontSize: 11, cursor: 'pointer' }}>
                  <input type="checkbox" checked={fog} onChange={(e) => setFog(e.target.checked)} />
                  Fog
                </label>
                <label style={{ display: 'flex', alignItems: 'center', gap: 4, color: 'var(--txt-2)', fontSize: 11, cursor: 'pointer' }}>
                  <input type="checkbox" checked={rain} onChange={(e) => setRain(e.target.checked)} />
                  Rain
                </label>
              </div>

              <div style={sectionHeaderStyle}>SENSORS</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>GNSS Noise</span>
                <input type="number" step={0.1} value={gnssNoise} onChange={(e) => setGnssNoise(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>m</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>AIS Loss</span>
                <input type="number" step={1} value={aisLoss} onChange={(e) => setAisLoss(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>%</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Radar Range</span>
                <input type="number" value={radarRange} onChange={(e) => setRadarRange(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>nm</span>
              </div>

              <div style={sectionHeaderStyle}>ODD / SHIP</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Hull Type</span>
                <select value={hullType} onChange={(e) => setHullType(e.target.value as 'FCB' | 'ASV')} style={inputStyle}>
                  <option value="FCB">FCB</option>
                  <option value="ASV">ASV</option>
                </select>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Displacement</span>
                <input type="number" value={displacement} onChange={(e) => setDisplacement(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>t</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Init Heading</span>
                <input type="number" value={initHeading} onChange={(e) => setInitHeading(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>deg</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Init SOG</span>
                <input type="number" step={0.1} value={initSog} onChange={(e) => setInitSog(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>kn</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Max Rudder</span>
                <input type="number" value={maxRudder} onChange={(e) => setMaxRudder(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>deg</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Max ROT</span>
                <input type="number" step={0.1} value={maxRot} onChange={(e) => setMaxRot(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>deg/s</span>
              </div>
            </>
          )}

          {/* ── Tab C: Run ──── */}
          {activeTab === 'run' && (
            <>
              <div style={sectionHeaderStyle}>TIMING</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Duration</span>
                <input type="number" value={duration} onChange={(e) => setDuration(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>s</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Sim Rate</span>
                <input type="number" step={0.1} value={simRate} onChange={(e) => setSimRate(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>×</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Random Seed</span>
                <input type="number" value={randomSeed} onChange={(e) => setRandomSeed(Number(e.target.value))} style={inputStyle} />
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>M2 Hz</span>
                <input type="number" value={m2Hz} onChange={(e) => setM2Hz(Number(e.target.value))} style={inputStyle} />
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>M5 Hz</span>
                <input type="number" value={m5Hz} onChange={(e) => setM5Hz(Number(e.target.value))} style={inputStyle} />
              </div>

              <div style={sectionHeaderStyle}>MODE</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Autonomy</span>
                <select value={autonomyLevel} onChange={(e) => setAutonomyLevel(e.target.value as 'D2' | 'D3' | 'D4')} style={inputStyle}>
                  <option value="D2">D2</option>
                  <option value="D3">D3</option>
                  <option value="D4">D4</option>
                </select>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>Operator</span>
                <select value={operatorRole} onChange={(e) => setOperatorRole(e.target.value as 'Observer' | 'Supervisor' | 'Commander')} style={inputStyle}>
                  <option value="Observer">Observer</option>
                  <option value="Supervisor">Supervisor</option>
                  <option value="Commander">Commander</option>
                </select>
              </div>

              <div style={sectionHeaderStyle}>IvP WEIGHTS</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>COLREG_AVOID</span>
                <input type="range" min={0} max={100} value={colregAvoidW} onChange={(e) => setColregAvoidW(Number(e.target.value))} style={{ flex: 1, accentColor: 'var(--c-phos)' }} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--c-phos)', minWidth: 28, textAlign: 'right' }}>{colregAvoidW}</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>MISSION_ETA</span>
                <input type="range" min={0} max={100} value={missionEtaW} onChange={(e) => setMissionEtaW(Number(e.target.value))} style={{ flex: 1, accentColor: 'var(--c-phos)' }} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--c-phos)', minWidth: 28, textAlign: 'right' }}>{missionEtaW}</span>
              </div>

              <div style={sectionHeaderStyle}>THRESHOLDS</div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>CPA Min</span>
                <input type="number" step={0.01} value={cpaMinThreshold} onChange={(e) => setCpaMinThreshold(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>nm</span>
              </div>
              <div style={fieldRowStyle}>
                <span style={labelStyle}>TCPA Trigger</span>
                <input type="number" step={0.1} value={tcpaTrigger} onChange={(e) => setTcpaTrigger(Number(e.target.value))} style={inputStyle} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>min</span>
              </div>

              <div style={sectionHeaderStyle}>FAULT INJECTION</div>
              <table style={{ width: '100%', borderCollapse: 'collapse', fontFamily: 'var(--f-mono)', fontSize: 10 }}>
                <thead>
                  <tr style={{ borderBottom: '1px solid var(--line-1)' }}>
                    <th style={{ textAlign: 'left', color: 'var(--txt-3)', padding: '2px 4px' }}></th>
                    <th style={{ textAlign: 'left', color: 'var(--txt-3)', padding: '2px 4px' }}>ID</th>
                    <th style={{ textAlign: 'right', color: 'var(--txt-3)', padding: '2px 4px' }}>At</th>
                    <th style={{ textAlign: 'right', color: 'var(--txt-3)', padding: '2px 4px' }}>Dur</th>
                  </tr>
                </thead>
                <tbody>
                  {FAULT_ROWS.map((f) => (
                    <tr key={f.id} style={{ borderBottom: '1px solid rgba(255,255,255,0.03)' }}>
                      <td style={{ padding: '2px 4px' }}><input type="checkbox" defaultChecked={f.checked} /></td>
                      <td style={{ color: 'var(--txt-1)', padding: '2px 4px' }}>{f.id}</td>
                      <td style={{ textAlign: 'right', color: 'var(--txt-2)', padding: '2px 4px' }}>{f.at}</td>
                      <td style={{ textAlign: 'right', color: 'var(--txt-2)', padding: '2px 4px' }}>{f.dur}</td>
                    </tr>
                  ))}
                </tbody>
              </table>

              <div style={sectionHeaderStyle}>PASS CRITERIA</div>
              <table style={{ width: '100%', borderCollapse: 'collapse', fontFamily: 'var(--f-mono)', fontSize: 10 }}>
                <thead>
                  <tr style={{ borderBottom: '1px solid var(--line-1)' }}>
                    <th style={{ textAlign: 'left', color: 'var(--txt-3)', padding: '2px 4px' }}>Metric</th>
                    <th style={{ textAlign: 'right', color: 'var(--txt-3)', padding: '2px 4px' }}>Target</th>
                  </tr>
                </thead>
                <tbody>
                  {PASS_ROWS.map((r) => (
                    <tr key={r.label} style={{ borderBottom: '1px solid rgba(255,255,255,0.03)' }}>
                      <td style={{ color: 'var(--txt-1)', padding: '2px 4px' }}>{r.label}</td>
                      <td style={{ textAlign: 'right', color: 'var(--c-phos)', padding: '2px 4px' }}>{r.target}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </>
          )}

          {/* ── Tab D: Summary ──── */}
          {activeTab === 'summary' && (
            <>
              <div style={sectionHeaderStyle}>SCENARIO SUMMARY</div>
              <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-2)', lineHeight: 1.8 }}>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: 'var(--txt-3)' }}>ID</span>
                  <span>{previewData?.scenarioId ?? '—'}</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: 'var(--txt-3)' }}>ENC Region</span>
                  <span>{previewData?.encRegion ?? '—'}</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: 'var(--txt-3)' }}>OS Position</span>
                  <span>{previewData?.ownShip ? `${previewData.ownShip.lat?.toFixed(4)}, ${previewData.ownShip.lon?.toFixed(4)}` : '—'}</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: 'var(--txt-3)' }}>OS Heading</span>
                  <span>{previewData?.ownShip ? `${previewData.ownShip.heading}°` : '—'}</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: 'var(--txt-3)' }}>OS SOG</span>
                  <span>{previewData?.ownShip ? `${previewData.ownShip.sog} kn` : '—'}</span>
                </div>
                {previewData?.targets?.map((t: any, i: number) => (
                  <div key={i} style={{ display: 'flex', justifyContent: 'space-between' }}>
                    <span style={{ color: 'var(--txt-3)' }}>TS-{i + 1}</span>
                    <span>{t.heading}° / {t.sog} kn</span>
                  </div>
                ))}
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: 'var(--txt-3)' }}>Duration</span>
                  <span>{duration}s</span>
                </div>
              </div>

              <div style={{ display: 'flex', flexDirection: 'column', gap: 8, marginTop: 20 }}>
                <button
                  onClick={onSave}
                  style={{
                    padding: '10px',
                    background: 'transparent',
                    border: '1px solid var(--line-2)',
                    color: 'var(--txt-1)',
                    borderRadius: 6,
                    fontFamily: 'var(--f-disp)',
                    fontSize: 11,
                    fontWeight: 700,
                    cursor: 'pointer',
                    letterSpacing: 1,
                  }}
                >
                  SAVE
                </button>
                <button
                  onClick={onValidate}
                  style={{
                    padding: '10px',
                    background: 'transparent',
                    border: '1px solid var(--line-2)',
                    color: 'var(--txt-1)',
                    borderRadius: 6,
                    fontFamily: 'var(--f-disp)',
                    fontSize: 11,
                    fontWeight: 700,
                    cursor: 'pointer',
                    letterSpacing: 1,
                  }}
                >
                  VALIDATE
                </button>
                <button
                  data-testid="run-action-btn"
                  onClick={onRun}
                  style={{
                    padding: '14px',
                    background: 'var(--c-phos)',
                    border: 'none',
                    color: '#000',
                    borderRadius: 8,
                    fontFamily: 'var(--f-disp)',
                    fontSize: 12,
                    fontWeight: 800,
                    cursor: 'pointer',
                    letterSpacing: 1,
                  }}
                >
                  RUN →
                </button>
              </div>
            </>
          )}
        </div>
      )}
    </div>
  );
}