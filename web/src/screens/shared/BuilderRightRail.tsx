import { useState } from 'react';

const IMAZU_CASES = [
  '01 Head-on','02 Cross 45°','03 Cross 90°','04 Cross 135°',
  '05 Cross 45°R','06 Cross 90°R','07 Cross 135°R','08 Cross 180°',
  '09 Overtake 0°','10 Overtake 22°','11 Overtake 45°','12 Overtake 90°',
  '13 Overtake 135°','14 Cross 45°','15 Cross 90°','16 Cross 135°',
  '17 Cross 180°','18 Overtake','19 Cross 22°','20 Cross 67°',
  '21 Cross 112°','22 Cross 157°',
];

function EncounterMini({ caseIdx }: { caseIdx: number }) {
  const angle = ((caseIdx % 8) * 45) * (Math.PI / 180);
  const cx = 24, cy = 24, r = 16;
  const tx = cx + r * Math.sin(angle);
  const ty = cy - r * Math.cos(angle);
  return (
    <svg width={48} height={48} viewBox="0 0 48 48">
      <polygon points={`${cx},${cy - 8} ${cx - 5},${cy + 6} ${cx + 5},${cx + 6}`}
               fill="#ef4444" />
      <polygon points={`${tx},${ty + 8} ${tx - 4},${ty - 5} ${tx + 4},${ty - 5}`}
               fill="#3b82f6" transform={`rotate(${caseIdx * 16}, ${tx}, ${ty})`} />
      <line x1={cx} y1={cy} x2={tx} y2={ty}
            stroke="#4b5563" strokeWidth="0.5" strokeDasharray="2,2" />
    </svg>
  );
}

function Field({ label, type = 'number', unit = '', defaultValue = '' }: {
  label: string; type?: string; unit?: string; defaultValue?: string | number;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 2, marginBottom: 10 }}>
      <label style={{ fontSize: 9, color: 'var(--txt-3)', fontFamily: 'var(--f-disp)',
                      letterSpacing: '0.1em', textTransform: 'uppercase' }}>
        {label}{unit && ` (${unit})`}
      </label>
      <input type={type} defaultValue={defaultValue} style={{
        background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
        color: 'var(--txt-1)', padding: '6px 8px', borderRadius: 4,
        fontFamily: 'var(--f-mono)', fontSize: 11, outline: 'none', width: '100%',
      }} />
    </div>
  );
}

function Select({ label, options }: { label: string; options: string[] }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 2, marginBottom: 10 }}>
      <label style={{ fontSize: 9, color: 'var(--txt-3)', fontFamily: 'var(--f-disp)',
                      letterSpacing: '0.1em', textTransform: 'uppercase' }}>{label}</label>
      <select style={{
        background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
        color: 'var(--txt-1)', padding: '6px 8px', borderRadius: 4,
        fontFamily: 'var(--f-mono)', fontSize: 11, outline: 'none', width: '100%',
      }}>
        {options.map((o) => <option key={o}>{o}</option>)}
      </select>
    </div>
  );
}

function Toggle({ label, defaultChecked = false }: { label: string; defaultChecked?: boolean }) {
  const [on, setOn] = useState(defaultChecked);
  return (
    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                  marginBottom: 8, cursor: 'pointer' }} onClick={() => setOn(!on)}>
      <span style={{ fontSize: 11, color: 'var(--txt-2)', fontFamily: 'var(--f-mono)' }}>{label}</span>
      <div style={{
        width: 32, height: 18, borderRadius: 9, position: 'relative',
        background: on ? 'var(--c-phos)' : 'var(--line-2)', transition: 'background 0.15s',
      }}>
        <div style={{
          position: 'absolute', top: 2, left: on ? 14 : 2,
          width: 14, height: 14, borderRadius: 7, background: '#fff',
          transition: 'left 0.15s',
        }} />
      </div>
    </div>
  );
}

function EncounterTab({ previewData }: { previewData: any }) {
  const [selected, setSelected] = useState(0);
  return (
    <div>
      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    marginBottom: 8, fontFamily: 'var(--f-disp)' }}>IMAZU-22 CASE</div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 4, marginBottom: 12 }}>
        {IMAZU_CASES.map((c, i) => (
          <div key={i} onClick={() => setSelected(i)} style={{
            border: `1px solid ${selected === i ? 'var(--c-phos)' : 'var(--line-1)'}`,
            borderRadius: 4, cursor: 'pointer', overflow: 'hidden',
            background: selected === i ? 'rgba(91,192,190,0.1)' : 'rgba(0,0,0,0.2)',
          }}>
            <EncounterMini caseIdx={i} />
            <div style={{ fontSize: 8, color: selected === i ? 'var(--c-phos)' : 'var(--txt-3)',
                          textAlign: 'center', padding: '2px 0', fontFamily: 'var(--f-mono)' }}>
              {c.split(' ')[0]}
            </div>
          </div>
        ))}
      </div>
      <div style={{ padding: '8px 0', borderTop: '1px solid var(--line-1)', marginBottom: 8 }}>
        <div style={{ fontSize: 10, color: 'var(--c-phos)', fontFamily: 'var(--f-mono)',
                      marginBottom: 6 }}>{IMAZU_CASES[selected]}</div>
      </div>
      <Field label="DCPA Target" unit="nm" defaultValue="0.50" />
      <Field label="TCPA Target" unit="min" defaultValue="10.0" />
      <Field label="TS Relative Bearing" unit="deg" defaultValue="0" />
      <Field label="TS Distance" unit="nm" defaultValue="7.0" />
      <Field label="TS SOG" unit="kn" defaultValue="10.0" />
      <Field label="TS COG" unit="deg" defaultValue="180" />
    </div>
  );
}

function EnvironmentTab() {
  return (
    <div>
      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    marginBottom: 6, fontFamily: 'var(--f-disp)' }}>WIND / SEA / CURRENT</div>
      <Select label="Beaufort" options={['0 – Calm','1','2','3 – Light','4','5 – Fresh','6','7 – Near gale']} />
      <Field label="Wind Direction" unit="deg" defaultValue="0" />
      <Field label="Wave Height Hs" unit="m" defaultValue="0.0" />
      <Field label="Wave Direction" unit="deg" defaultValue="0" />
      <Field label="Current Speed" unit="kn" defaultValue="0.0" />
      <Field label="Current Direction" unit="deg" defaultValue="0" />

      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    margin: '10px 0 6px', fontFamily: 'var(--f-disp)' }}>VISIBILITY / SENSORS</div>
      <Select label="Daypart" options={['Day','Night','Dawn','Dusk']} />
      <Field label="Visibility" unit="nm" defaultValue="10.0" />
      <Toggle label="Fog" />
      <Toggle label="Rain" />
      <Field label="GNSS Noise σ" unit="m" defaultValue="0.0" />
      <Field label="AIS Loss Rate" unit="%" defaultValue="0" />
      <Field label="Radar Range" unit="nm" defaultValue="12.0" />

      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    margin: '10px 0 6px', fontFamily: 'var(--f-disp)' }}>ODD / OWN SHIP</div>
      <Select label="Hull Type" options={['FCB','ASV']} />
      <Field label="Displacement" unit="t" defaultValue="450" />
      <Field label="Initial Heading" unit="deg" defaultValue="0" />
      <Field label="Initial SOG" unit="kn" defaultValue="10.0" />
      <Field label="Max Rudder" unit="deg" defaultValue="35" />
      <Field label="Max ROT" unit="deg/s" defaultValue="2.0" />
    </div>
  );
}

const FAULT_ROWS = [
  { type: 'AIS_DROPOUT',    t: 60,  dur: 30 },
  { type: 'RADAR_SPIKE',    t: 120, dur: 10 },
  { type: 'DIST_STEP',      t: 200, dur: 60 },
  { type: 'SENSOR_FREEZE',  t: 300, dur: 20 },
];

const PASS_ROWS = [
  { label: 'CPA min',         threshold: '0.50 nm' },
  { label: 'COLREGs',         threshold: '100%' },
  { label: 'Path deviation',  threshold: '≤2 nm' },
  { label: 'ToR response',    threshold: '≤60 s' },
  { label: 'M5 solve p95',    threshold: '≤500 ms' },
  { label: 'ASDR integrity',  threshold: '100%' },
];

function RunTab() {
  const [faultsOn, setFaultsOn] = useState<Record<number, boolean>>({});
  return (
    <div>
      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    marginBottom: 6, fontFamily: 'var(--f-disp)' }}>TIMING / CLOCK</div>
      <Field label="Duration" unit="s" defaultValue="700" />
      <Field label="Sim Rate" unit="×" defaultValue="1.0" />
      <Field label="Random Seed" defaultValue="42" />
      <Field label="M2 Hz" defaultValue="10" />
      <Field label="M5 Hz" defaultValue="4" />

      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    margin: '10px 0 6px', fontFamily: 'var(--f-disp)' }}>MODE / IVP WEIGHTS</div>
      <Select label="Autonomy Level" options={['D2 – Remote control','D3 – Supervised','D4 – Full']} />
      <Select label="Operator Role" options={['Observer','Supervisor','Commander']} />
      <div style={{ marginBottom: 10 }}>
        <label style={{ fontSize: 9, color: 'var(--txt-3)', fontFamily: 'var(--f-disp)',
                        letterSpacing: '0.1em', textTransform: 'uppercase' }}>COLREG_AVOID</label>
        <input type="range" min={0} max={100} defaultValue={80}
               style={{ width: '100%', accentColor: 'var(--c-phos)' }} />
      </div>
      <div style={{ marginBottom: 10 }}>
        <label style={{ fontSize: 9, color: 'var(--txt-3)', fontFamily: 'var(--f-disp)',
                        letterSpacing: '0.1em', textTransform: 'uppercase' }}>MISSION_ETA</label>
        <input type="range" min={0} max={100} defaultValue={50}
               style={{ width: '100%', accentColor: 'var(--c-phos)' }} />
      </div>
      <Field label="CPA Min Threshold" unit="nm" defaultValue="0.50" />
      <Field label="TCPA Trigger" unit="min" defaultValue="10.0" />

      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    margin: '10px 0 6px', fontFamily: 'var(--f-disp)' }}>FAULT INJECTION</div>
      <div style={{ border: '1px solid var(--line-1)', borderRadius: 4, overflow: 'hidden', marginBottom: 12 }}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 40px 40px 32px',
                      padding: '4px 6px', background: 'rgba(0,0,0,0.3)',
                      fontSize: 8, color: 'var(--txt-3)', fontFamily: 'var(--f-mono)' }}>
          <span>TYPE</span><span>T(s)</span><span>DUR</span><span>ON</span>
        </div>
        {FAULT_ROWS.map((r, i) => (
          <div key={i} style={{
            display: 'grid', gridTemplateColumns: '1fr 40px 40px 32px',
            padding: '5px 6px', borderTop: '1px solid var(--line-1)',
            fontSize: 9, color: 'var(--txt-2)', fontFamily: 'var(--f-mono)',
            alignItems: 'center',
          }}>
            <span style={{ fontSize: 8 }}>{r.type}</span>
            <span>{r.t}</span>
            <span>{r.dur}</span>
            <input type="checkbox" checked={!!faultsOn[i]}
                   onChange={(e) => setFaultsOn((prev) => ({ ...prev, [i]: e.target.checked }))}
                   style={{ accentColor: 'var(--c-phos)' }} />
          </div>
        ))}
      </div>

      <div style={{ fontSize: 9, color: 'var(--txt-3)', letterSpacing: '0.1em',
                    marginBottom: 6, fontFamily: 'var(--f-disp)' }}>PASS CRITERIA</div>
      <div style={{ border: '1px solid var(--line-1)', borderRadius: 4, overflow: 'hidden' }}>
        {PASS_ROWS.map((r, i) => (
          <div key={i} style={{
            display: 'flex', justifyContent: 'space-between', alignItems: 'center',
            padding: '5px 8px', borderTop: i > 0 ? '1px solid var(--line-1)' : 'none',
            fontSize: 9, fontFamily: 'var(--f-mono)',
          }}>
            <span style={{ color: 'var(--txt-2)' }}>{r.label}</span>
            <span style={{ color: 'var(--c-phos)' }}>{r.threshold}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

function SummaryTab({ previewData, onSave, onValidate, onRun }: {
  previewData: any; onSave: () => void; onValidate: () => void; onRun: () => void;
}) {
  const os = previewData?.ownShip;
  const ts = previewData?.targets?.[0];
  const row = (label: string, val: string) => (
    <div style={{ display: 'flex', justifyContent: 'space-between', padding: '4px 0',
                  borderBottom: '1px solid var(--line-1)', fontSize: 10, fontFamily: 'var(--f-mono)' }}>
      <span style={{ color: 'var(--txt-3)' }}>{label}</span>
      <span style={{ color: 'var(--txt-1)' }}>{val}</span>
    </div>
  );
  return (
    <div>
      <div style={{ marginBottom: 12 }}>
        {row('Scenario', 'imazu-01-ho-v1.0')}
        {row('ENC Region', 'trondelag')}
        {row('Duration', '700 s')}
        {os && row('OS', `${os.lat.toFixed(3)}°N ${os.lon.toFixed(3)}°E HDG ${os.heading}° ${os.sog}kn`)}
        {ts && row('TS', `${ts.lat.toFixed(3)}°N ${ts.lon.toFixed(3)}°E HDG ${ts.heading}° ${ts.sog}kn`)}
      </div>
      <div style={{ display: 'flex', gap: 6, marginBottom: 8 }}>
        <button onClick={onSave} style={btnStyle('line')}>SAVE</button>
        <button onClick={onValidate} style={btnStyle('line')}>VALIDATE</button>
      </div>
      <button onClick={onRun} style={btnStyle('phos')}>RUN →</button>
    </div>
  );
}

const btnStyle = (variant: 'line' | 'phos'): React.CSSProperties => ({
  flex: 1, padding: '10px 0', borderRadius: 4, cursor: 'pointer',
  fontFamily: 'var(--f-disp)', fontSize: 10, fontWeight: 700,
  letterSpacing: '0.12em', textAlign: 'center',
  width: variant === 'phos' ? '100%' : undefined,
  background: variant === 'phos' ? 'var(--c-phos)' : 'transparent',
  color: variant === 'phos' ? 'var(--bg-0)' : 'var(--txt-1)',
  border: variant === 'phos' ? '1px solid var(--c-phos)' : '1px solid var(--line-2)',
});

const TABS = [
  { id: 'encounter',    label: 'Encounter',    glyph: '⊕' },
  { id: 'environment',  label: 'Environment',  glyph: '⛅' },
  { id: 'run',          label: 'Run',          glyph: '▶' },
  { id: 'summary',      label: 'Summary',      glyph: '☰' },
] as const;

type TabId = typeof TABS[number]['id'];

export interface BuilderRightRailProps {
  previewData: any;
  onRun: () => void;
  onSave: () => void;
  onValidate: () => void;
}

export function BuilderRightRail({ previewData, onRun, onSave, onValidate }: BuilderRightRailProps) {
  const [activeTab, setActiveTab] = useState<TabId | null>(null);
  const isExpanded = activeTab !== null;

  const handleTabClick = (id: TabId) => {
    setActiveTab(prev => prev === id ? null : id);
  };

  return (
    <div style={{
      width: isExpanded ? '320px' : '48px',
      flexShrink: 0,
      background: '#0a0f18',
      borderLeft: '1px solid var(--line-2)',
      display: 'flex',
      flexDirection: 'row',
      transition: 'width 0.2s cubic-bezier(0.4, 0, 0.2, 1)',
      overflow: 'hidden',
      position: 'relative',
    }}>

      <div style={{
        width: 48, flexShrink: 0, display: 'flex', flexDirection: 'column',
        alignItems: 'center', paddingTop: 20, gap: 4,
        borderRight: isExpanded ? '1px solid var(--line-1)' : 'none',
      }}>
        {TABS.map((tab) => (
          <button key={tab.id} title={tab.label} onClick={() => handleTabClick(tab.id)} style={{
            width: 36, height: 36, borderRadius: 6, border: 'none', cursor: 'pointer',
            background: activeTab === tab.id ? 'rgba(91,192,190,0.15)' : 'transparent',
            color: activeTab === tab.id ? 'var(--c-phos)' : 'var(--txt-3)',
            display: 'flex', alignItems: 'center',
            justifyContent: 'center',
          }}>
            <span style={{ fontSize: 16 }}>{tab.glyph}</span>
          </button>
        ))}

        {isExpanded && (
          <button title="Collapse panel" onClick={() => setActiveTab(null)} style={{
            marginTop: 'auto', marginBottom: 16, width: 36, height: 24, borderRadius: 4,
            border: '1px solid var(--line-1)', background: 'transparent',
            color: 'var(--txt-3)', cursor: 'pointer', fontSize: 14,
          }}>›</button>
        )}
      </div>

      {isExpanded && (
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
          <div style={{
            padding: '14px 12px 10px', borderBottom: '1px solid var(--line-1)',
            fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 700,
            color: 'var(--c-phos)', letterSpacing: '0.12em',
          }}>
            {TABS.find(t => t.id === activeTab)?.label.toUpperCase()}
          </div>

          <div style={{ flex: 1, overflowY: 'auto', padding: '10px 12px 24px' }}>
            {activeTab === 'encounter'   && <EncounterTab previewData={previewData} />}
            {activeTab === 'environment' && <EnvironmentTab />}
            {activeTab === 'run'         && <RunTab />}
            {activeTab === 'summary'     && (
              <SummaryTab previewData={previewData}
                          onSave={onSave} onValidate={onValidate} onRun={onRun} />
            )}
          </div>
        </div>
      )}
    </div>
  );
}