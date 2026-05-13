// sil-builder.jsx — Screen ①: Scenario Builder
// 3 tabs: Encounter Geometry (Imazu grid), Environment, Run Settings

const { useState: useState_B } = React;

// Small re-usable form field
function Field({ label, hint, children, width }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: width }}>
      <Label color={COL.t3}>{label}</Label>
      {children}
      {hint && <Mono size={9} color={COL.t3}>{hint}</Mono>}
    </div>
  );
}
function NumIn({ value, onChange, unit, w = 80 }) {
  return (
    <div style={{ display: 'inline-flex', alignItems: 'baseline', gap: 4, border: `1px solid ${COL.line2}`, background: COL.bg0, padding: '4px 8px', width: w }}>
      <input type="text" value={value} onChange={e => onChange(e.target.value)} style={{
        background: 'transparent', border: 'none', outline: 'none', color: COL.t0,
        fontFamily: FONT_MONO, fontSize: 13, width: '100%', padding: 0, fontWeight: 600,
      }} />
      {unit && <Mono size={9} color={COL.t3}>{unit}</Mono>}
    </div>
  );
}
function Seg({ value, onChange, options, w }) {
  return (
    <div style={{ display: 'inline-flex', border: `1px solid ${COL.line2}` }}>
      {options.map(o => {
        const sel = value === o.v;
        return (
          <div key={o.v} onClick={() => onChange(o.v)} style={{
            padding: '5px 10px', cursor: 'pointer',
            background: sel ? COL.phos : 'transparent',
            color: sel ? COL.bg0 : COL.t2,
            fontFamily: FONT_DISP, fontSize: 10.5, letterSpacing: '0.16em', fontWeight: 600,
            borderRight: `1px solid ${COL.line1}`, minWidth: w,
            textAlign: 'center', textTransform: 'uppercase',
          }}>{o.l}</div>
        );
      })}
    </div>
  );
}
function Sw({ on, onChange, lbl }) {
  return (
    <div onClick={() => onChange(!on)} style={{ display: 'inline-flex', alignItems: 'center', gap: 8, cursor: 'pointer' }}>
      <div style={{ width: 28, height: 14, border: `1px solid ${on ? COL.stbd : COL.line2}`, background: on ? `${COL.stbd}33` : COL.bg0, position: 'relative' }}>
        <div style={{ position: 'absolute', top: 1, left: on ? 14 : 1, width: 11, height: 10, background: on ? COL.stbd : COL.t3, transition: 'left 0.12s' }} />
      </div>
      <Mono size={10.5} color={on ? COL.t0 : COL.t2}>{lbl}</Mono>
    </div>
  );
}

// ── Tab 1: Encounter Geometry (Imazu grid + selected case detail) ─
function TabEncounter({ scenario, setScenario }) {
  const sel = IMAZU_CASES.find(c => c.id === scenario.imazuId) || IMAZU_CASES[2];

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 380px', gap: 16, height: '100%' }}>
      {/* Imazu grid */}
      <div style={{ overflowY: 'auto', paddingRight: 8 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', marginBottom: 10 }}>
          <Disp size={13} color={COL.t1} tracking="0.16em" weight={600}>22 IMAZU CASES · 经典会遇集</Disp>
          <Mono size={10} color={COL.t3}>来源 · Imazu 1987 + COLREGs 扩展 (含 R9 / R13 / R14 / R15 / R17)</Mono>
        </div>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(154px, 1fr))', gap: 8 }}>
          {IMAZU_CASES.map(c => (
            <ImazuThumb key={c.id} caseData={c} selected={c.id === scenario.imazuId}
              onClick={() => setScenario({ ...scenario, imazuId: c.id, name: `IM${String(c.id).padStart(2,'0')}_${c.name.replace(/[^A-Za-z0-9]+/g,'_')}` })} />
          ))}
        </div>
      </div>

      {/* Selected detail */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
        <Panel title="选中场景 · 详情" accent={COL.phos}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
            <Disp size={18} color={COL.t0} weight={700} tracking="0.10em">IM{String(sel.id).padStart(2,'0')} · {sel.name}</Disp>
            <Mono size={11} color={COL.warn}>{sel.rule}</Mono>
          </div>
          <Mono size={10} color={COL.t2} style={{ marginTop: 4, display: 'block' }}>
            目标数 = {sel.ships.length} · 让路 {sel.ships.filter(s=>s.role==='give-way').length} · 直航 {sel.ships.filter(s=>s.role==='stand-on').length}
          </Mono>
          <div style={{ marginTop: 10, background: COL.bg0, border: `1px solid ${COL.line1}`, padding: 8 }}>
            <svg viewBox="0 0 100 100" style={{ width: '100%', height: 220, background: '#050810' }}>
              <circle cx="50" cy="75" r="28" fill="none" stroke={COL.line2} strokeWidth="0.5" strokeDasharray="2 3" />
              <circle cx="50" cy="75" r="56" fill="none" stroke={COL.line2} strokeWidth="0.5" strokeDasharray="2 3" opacity="0.6" />
              <line x1="50" y1="75" x2="50" y2="15" stroke={COL.phos} strokeWidth="0.6" opacity="0.5" />
              <text x="48" y="13" fontFamily={FONT_MONO} fontSize="4" fill={COL.phos}>N</text>
              {sel.ships.map((s,i)=>{
                const c = s.role === 'give-way' ? COL.danger : s.role === 'stand-on' ? COL.warn : COL.info;
                const rad = (s.h - 90) * Math.PI/180;
                return (
                  <g key={i}>
                    <line x1={s.x} y1={s.y} x2={s.x+Math.cos(rad)*14} y2={s.y+Math.sin(rad)*14} stroke={c} strokeWidth="0.8" />
                    <g transform={`translate(${s.x},${s.y}) rotate(${s.h})`}>
                      <path d="M 0 -4 L 3 3 L 0 1 L -3 3 Z" fill="none" stroke={c} strokeWidth="1" />
                    </g>
                    <text x={s.x+5} y={s.y-3} fontFamily={FONT_DISP} fontSize="3.5" fill={c}>T0{i+1}</text>
                  </g>
                );
              })}
              <g transform="translate(50,75)"><path d="M 0 -4 L 3 3 L 0 1 L -3 3 Z" fill={COL.phos} /></g>
              <text x="53" y="78" fontFamily={FONT_DISP} fontSize="3.5" fill={COL.phos}>OWN</text>
            </svg>
          </div>
          <div style={{ marginTop: 10, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
            {sel.ships.map((s,i)=>{
              const c = s.role === 'give-way' ? COL.danger : s.role === 'stand-on' ? COL.warn : COL.info;
              return (
                <div key={i} style={{ padding: '6px 8px', background: COL.bg0, borderLeft: `2px solid ${c}` }}>
                  <Disp size={10} color={COL.t0} weight={600} tracking="0.10em">T0{i+1}</Disp>
                  <Mono size={9.5} color={c} style={{ display: 'block' }}>{s.role.toUpperCase()}</Mono>
                  <Mono size={9} color={COL.t2}>HDG {s.h}° · pos ({s.x},{s.y})</Mono>
                </div>
              );
            })}
          </div>
        </Panel>
        <Panel title="可选 · 目标参数微调" accent={COL.line3}>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
            <Field label="目标船速范围">
              <Mono size={11} color={COL.t1}>10.0 – 18.0 kn</Mono>
            </Field>
            <Field label="初始距离">
              <Mono size={11} color={COL.t1}>4.5 – 6.0 nm</Mono>
            </Field>
            <Field label="意图扰动">
              <Mono size={11} color={COL.warn}>σ_HDG = 4° · σ_SOG = 0.8 kn</Mono>
            </Field>
            <Field label="意图突变事件">
              <Mono size={11} color={COL.t1}>1 次 @ T+180s</Mono>
            </Field>
          </div>
        </Panel>
      </div>
    </div>
  );
}

// ── Tab 2: Environment ─────────────────────────────────────────────
function TabEnvironment({ scenario, setScenario }) {
  const e = scenario.env || {};
  const setE = (patch) => setScenario({ ...scenario, env: { ...e, ...patch } });
  return (
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, height: '100%', overflowY: 'auto', paddingRight: 4 }}>
      <Panel title="海况 · WIND / SEA / CURRENT" accent={COL.info}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14 }}>
          <Field label="风速 (Beaufort)" hint="0 平静 — 12 飓风">
            <Seg value={e.beaufort || 4} onChange={v => setE({ beaufort: v })}
              options={[{v:2,l:'2'},{v:4,l:'4'},{v:6,l:'6'},{v:8,l:'8'}]} />
          </Field>
          <Field label="风向 (来自)">
            <NumIn value={e.windDir ?? '270'} onChange={v => setE({ windDir: v })} unit="°T" />
          </Field>
          <Field label="浪高 (Hs)">
            <NumIn value={e.hs ?? '1.8'} onChange={v => setE({ hs: v })} unit="m" />
          </Field>
          <Field label="浪向 (来自)">
            <NumIn value={e.waveDir ?? '258'} onChange={v => setE({ waveDir: v })} unit="°T" />
          </Field>
          <Field label="表层流速">
            <NumIn value={e.curSpd ?? '0.7'} onChange={v => setE({ curSpd: v })} unit="kn" />
          </Field>
          <Field label="流向 (流向去)">
            <NumIn value={e.curDir ?? '022'} onChange={v => setE({ curDir: v })} unit="°T" />
          </Field>
        </div>
      </Panel>

      <Panel title="能见度与传感器" accent={COL.info}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14 }}>
          <Field label="时段">
            <Seg value={e.daypart || 'NIGHT'} onChange={v => setE({ daypart: v })}
              options={[{v:'DAY',l:'DAY'},{v:'DUSK',l:'DUSK'},{v:'NIGHT',l:'NIGHT'}]} />
          </Field>
          <Field label="气象能见度">
            <NumIn value={e.visNm ?? '4.0'} onChange={v => setE({ visNm: v })} unit="nm" />
          </Field>
          <Field label="雾 / 降雨注入">
            <Seg value={e.weather || 'CLR'} onChange={v => setE({ weather: v })}
              options={[{v:'CLR',l:'CLR'},{v:'FOG',l:'FOG'},{v:'RAIN',l:'RAIN'}]} />
          </Field>
          <Field label="GNSS 噪声">
            <NumIn value={e.gnss ?? '1.5'} onChange={v => setE({ gnss: v })} unit="m σ" />
          </Field>
          <Field label="AIS 中断率">
            <NumIn value={e.aisLoss ?? '2.0'} onChange={v => setE({ aisLoss: v })} unit="%" />
          </Field>
          <Field label="雷达探测范围">
            <NumIn value={e.radarNm ?? '12.0'} onChange={v => setE({ radarNm: v })} unit="nm" />
          </Field>
        </div>
      </Panel>

      <Panel title="ODD 区域配置" accent={COL.warn}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14 }}>
          <Field label="主子域 (Primary)">
            <Seg value={e.odd || 'A'} onChange={v => setE({ odd: v })}
              options={[{v:'A',l:'A · OPEN'},{v:'B',l:'B · TSS'},{v:'C',l:'C · RESTRICTED'}]} />
          </Field>
          <Field label="预期 EDGE 事件">
            <NumIn value={e.edgeCount ?? '1'} onChange={v => setE({ edgeCount: v })} unit="次" />
          </Field>
          <Field label="包络半径">
            <NumIn value={e.envR ?? '5.0'} onChange={v => setE({ envR: v })} unit="nm" />
          </Field>
          <Field label="目标合规分阈值">
            <NumIn value={e.confTh ?? '0.65'} onChange={v => setE({ confTh: v })} unit="—" />
          </Field>
        </div>
      </Panel>

      <Panel title="本船 · 动力学" accent={COL.stbd}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14 }}>
          <Field label="船型">
            <Seg value={e.hull || 'FCB45'} onChange={v => setE({ hull: v })}
              options={[{v:'FCB45',l:'FCB-45M'},{v:'CON180',l:'CONT-180M'},{v:'TUG28',l:'TUG-28M'}]} />
          </Field>
          <Field label="排水量">
            <NumIn value={e.disp ?? '1240'} onChange={v => setE({ disp: v })} unit="t" />
          </Field>
          <Field label="初始 HDG">
            <NumIn value={e.hdg0 ?? '005'} onChange={v => setE({ hdg0: v })} unit="°T" />
          </Field>
          <Field label="初始 SOG">
            <NumIn value={e.sog0 ?? '18.4'} onChange={v => setE({ sog0: v })} unit="kn" />
          </Field>
          <Field label="最大舵角">
            <NumIn value={e.rudMax ?? '35'} onChange={v => setE({ rudMax: v })} unit="°" />
          </Field>
          <Field label="ROT 上限">
            <NumIn value={e.rotMax ?? '8.0'} onChange={v => setE({ rotMax: v })} unit="°/s" />
          </Field>
        </div>
      </Panel>
    </div>
  );
}

// ── Tab 3: Run Settings ────────────────────────────────────────────
function TabRun({ scenario, setScenario }) {
  const r = scenario.run || {};
  const setR = (patch) => setScenario({ ...scenario, run: { ...r, ...patch } });
  return (
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, height: '100%', overflowY: 'auto' }}>
      <Panel title="时序 · 时钟 · 随机性" accent={COL.phos}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14 }}>
          <Field label="时长">
            <NumIn value={scenario.duration} onChange={v => setScenario({ ...scenario, duration: v })} unit="s" />
          </Field>
          <Field label="实时倍率">
            <Seg value={r.rate || '1x'} onChange={v => setR({ rate: v })}
              options={[{v:'0.5x',l:'½×'},{v:'1x',l:'1×'},{v:'2x',l:'2×'},{v:'4x',l:'4×'}]} />
          </Field>
          <Field label="随机种子">
            <NumIn value={scenario.seed} onChange={v => setScenario({ ...scenario, seed: v })} w={130} />
          </Field>
          <Field label="M2 WorldState 频率">
            <Seg value={r.m2Hz || 10} onChange={v => setR({ m2Hz: v })}
              options={[{v:5,l:'5 Hz'},{v:10,l:'10 Hz'},{v:20,l:'20 Hz'}]} />
          </Field>
          <Field label="M5 规划频率">
            <Seg value={r.m5Hz || 2} onChange={v => setR({ m5Hz: v })}
              options={[{v:1,l:'1 Hz'},{v:2,l:'2 Hz'},{v:5,l:'5 Hz'}]} />
          </Field>
          <Field label="UI_State 频率">
            <Mono size={11} color={COL.t1}>50 Hz (固定)</Mono>
          </Field>
        </div>
      </Panel>

      <Panel title="模式与决策权重 (M4 IvP)" accent={COL.info}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14 }}>
          <Field label="起始自主等级">
            <Seg value={r.lvl0 || 'D3'} onChange={v => setR({ lvl0: v })}
              options={[{v:'D2',l:'D2'},{v:'D3',l:'D3'},{v:'D4',l:'D4'}]} />
          </Field>
          <Field label="操作员角色">
            <Seg value={r.role || 'CAPTAIN'} onChange={v => setR({ role: v })}
              options={[{v:'CAPTAIN',l:'CAPT'},{v:'OPERATOR',l:'ROC OP'}]} />
          </Field>
          <Field label="COLREGS_AVOID 权重">
            <NumIn value={r.wColreg ?? '0.71'} onChange={v => setR({ wColreg: v })} unit="—" />
          </Field>
          <Field label="MISSION_ETA 权重">
            <NumIn value={r.wEta ?? '0.07'} onChange={v => setR({ wEta: v })} unit="—" />
          </Field>
          <Field label="CPA 安全限">
            <NumIn value={r.cpaMin ?? '0.50'} onChange={v => setR({ cpaMin: v })} unit="nm" />
          </Field>
          <Field label="TCPA 触发">
            <NumIn value={r.tcpaTrig ?? '8'} onChange={v => setR({ tcpaTrig: v })} unit="min" />
          </Field>
        </div>
      </Panel>

      <Panel title="故障注入剧本 (Pre-scheduled)" accent={COL.danger}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          {[
            { t:'180s', m:'M2', kind:'AIS_DROP',    sev:'WARN',  on:true,  d:'目标 T01 AIS 中断 30s' },
            { t:'240s', m:'M7', kind:'HEARTBEAT_2', sev:'CRIT',  on:false, d:'M7 心跳丢帧 2 帧 (强制 ToR)' },
            { t:'300s', m:'M3', kind:'GNSS_BIAS',   sev:'WARN',  on:true,  d:'GNSS 突现 12m 偏置' },
            { t:'420s', m:'M5', kind:'SOLVE_TO',    sev:'WARN',  on:false, d:'M5 MPC 求解超时 (>50ms)' },
          ].map((f,i)=>(
            <div key={i} style={{
              display: 'grid', gridTemplateColumns: '54px 50px 130px 60px 1fr auto', gap: 8, alignItems: 'center',
              padding: '6px 10px', background: COL.bg0, borderLeft: `2px solid ${f.sev==='CRIT'?COL.danger:COL.warn}`,
            }}>
              <Mono size={11} color={COL.warn}>{f.t}</Mono>
              <Disp size={10} color={COL.t0} weight={600} tracking="0.12em">{f.m}</Disp>
              <Mono size={10.5} color={COL.t1}>{f.kind}</Mono>
              <Disp size={9.5} color={f.sev==='CRIT'?COL.danger:COL.warn} weight={600} tracking="0.14em">{f.sev}</Disp>
              <span style={{ fontFamily: FONT_BODY, fontSize: 11, color: COL.t2 }}>{f.d}</span>
              <Sw on={f.on} onChange={() => {}} lbl={f.on?'ON':'OFF'} />
            </div>
          ))}
          <div style={{ marginTop: 4, padding: '6px 10px', border: `1px dashed ${COL.line3}`, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <Mono size={10} color={COL.t3}>+ 新增故障节点 (运行时仍可手动注入)</Mono>
            <Disp size={10} color={COL.phos} tracking="0.14em">ADD</Disp>
          </div>
        </div>
      </Panel>

      <Panel title="评估指标 (Pass Criteria)" accent={COL.stbd}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
          {[
            ['CPA_min',        '≥ 0.40 nm',     'all targets'],
            ['COLREGs 合规率', '≥ 0.95',         'M6 SAT-2 cards'],
            ['路径偏离',       '≤ 1.20 nm',     'vs planned WP track'],
            ['ToR 应答时延',   '≤ 35 s',         'F-NEW-004'],
            ['M5 求解时延 p95','≤ 30 ms',       'mid-MPC'],
            ['ASDR 完整性',    '100%',           'SHA-256 chain'],
          ].map((row,i)=>(
            <div key={i} style={{ display: 'grid', gridTemplateColumns: '1fr 110px 1fr', gap: 8, padding: '5px 0', borderBottom: i<5?`1px dashed ${COL.line1}`:'none' }}>
              <Mono size={11} color={COL.t1}>{row[0]}</Mono>
              <Mono size={11} color={COL.stbd} weight={600}>{row[1]}</Mono>
              <Mono size={10} color={COL.t3}>{row[2]}</Mono>
            </div>
          ))}
        </div>
      </Panel>
    </div>
  );
}

// ── Container ──────────────────────────────────────────────────────
function ScenarioBuilder({ scenario, setScenario, onNext }) {
  const [tab, setTab] = useState_B('encounter');
  const tabs = [
    { id: 'encounter', code: 'A', label: 'ENCOUNTER',  zh: '会遇几何 · Imazu 22' },
    { id: 'env',       code: 'B', label: 'ENVIRONMENT',zh: '海况 · 传感器 · 本船' },
    { id: 'run',       code: 'C', label: 'RUN',        zh: '时序 · 故障 · 评估' },
  ];
  const sel = IMAZU_CASES.find(c => c.id === scenario.imazuId) || IMAZU_CASES[0];

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '220px 1fr 320px', height: '100%' }}>
      {/* Left: section sidebar */}
      <div style={{ background: COL.bg1, borderRight: `1px solid ${COL.line2}`, padding: '16px 0', display: 'flex', flexDirection: 'column' }}>
        <Label color={COL.t3} style={{ padding: '0 16px', marginBottom: 10 }}>SCENARIO STEPS</Label>
        {tabs.map(t => {
          const active = tab === t.id;
          return (
            <div key={t.id} onClick={() => setTab(t.id)} style={{
              padding: '12px 16px', cursor: 'pointer',
              borderLeft: active ? `3px solid ${COL.phos}` : '3px solid transparent',
              background: active ? COL.bg0 : 'transparent',
              display: 'flex', alignItems: 'center', gap: 12,
            }}>
              <div style={{
                width: 22, height: 22, border: `1px solid ${active ? COL.phos : COL.line2}`,
                color: active ? COL.phos : COL.t3, display: 'flex', alignItems: 'center', justifyContent: 'center',
                fontFamily: FONT_DISP, fontSize: 11, fontWeight: 700,
              }}>{t.code}</div>
              <div>
                <Disp size={11.5} color={active ? COL.t0 : COL.t1} weight={600} tracking="0.14em">{t.label}</Disp>
                <div style={{ fontFamily: FONT_BODY, fontSize: 10, color: COL.t3, marginTop: 1 }}>{t.zh}</div>
              </div>
            </div>
          );
        })}
        <div style={{ flex: 1 }} />
        <div style={{ padding: '0 16px', marginTop: 12 }}>
          <Label color={COL.t3} style={{ marginBottom: 6 }}>SCENARIO LIBRARY</Label>
          {['IM03_Crossing_GiveWay_v2','IM07_Overtaken_FogA','IM14_Triple_Conflict','IM22_Restricted_Narrow'].map((n,i)=>(
            <div key={i} style={{ padding: '5px 8px', marginBottom: 3, background: i===0?COL.bg0:'transparent', borderLeft: i===0?`2px solid ${COL.phos}`:`2px solid transparent` }}>
              <Mono size={10} color={i===0?COL.t0:COL.t2}>{n}</Mono>
            </div>
          ))}
        </div>
      </div>

      {/* Main content */}
      <div style={{ padding: 18, overflow: 'hidden' }}>
        {tab === 'encounter' && <TabEncounter scenario={scenario} setScenario={setScenario} />}
        {tab === 'env'       && <TabEnvironment scenario={scenario} setScenario={setScenario} />}
        {tab === 'run'       && <TabRun scenario={scenario} setScenario={setScenario} />}
      </div>

      {/* Right: scenario summary + actions */}
      <div style={{ background: COL.bg1, borderLeft: `1px solid ${COL.line2}`, padding: 16, display: 'flex', flexDirection: 'column', gap: 12 }}>
        <Label color={COL.phos}>SCENARIO SUMMARY</Label>
        <div style={{ background: COL.bg0, border: `1px solid ${COL.line2}`, padding: 12 }}>
          <Disp size={13} color={COL.t0} weight={700} tracking="0.12em">{scenario.name}</Disp>
          <Mono size={9.5} color={COL.t3} style={{ display: 'block', marginTop: 2 }}>SHA: 7f3a · v1.1.2 · author: capt.wei</Mono>
          <div style={{ height: 1, background: COL.line1, margin: '10px 0' }} />
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10, rowGap: 8 }}>
            <Field label="Imazu"><Mono size={11} color={COL.t0}>IM{String(sel.id).padStart(2,'0')} · {sel.rule}</Mono></Field>
            <Field label="Targets"><Mono size={11} color={COL.t0}>{sel.ships.length}</Mono></Field>
            <Field label="Duration"><Mono size={11} color={COL.t0}>{scenario.duration}s</Mono></Field>
            <Field label="Seed"><Mono size={11} color={COL.t0}>{scenario.seed}</Mono></Field>
            <Field label="ODD"><Mono size={11} color={COL.t0}>A · OPEN</Mono></Field>
            <Field label="Day"><Mono size={11} color={COL.t0}>NIGHT</Mono></Field>
          </div>
        </div>

        <Label color={COL.t3} style={{ marginTop: 4 }}>VALIDATION</Label>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 5 }}>
          {[
            ['几何参数完整',    true],
            ['ODD 包络一致',    true],
            ['评估指标已配置',  true],
            ['故障剧本已审核',  false],
          ].map((row,i)=>(
            <div key={i} style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '4px 8px', background: COL.bg0 }}>
              <span style={{ fontFamily: FONT_BODY, fontSize: 11, color: COL.t1 }}>{row[0]}</span>
              <Mono size={10} color={row[1]?COL.stbd:COL.warn} weight={700}>{row[1]?'✓ OK':'⚠ 待确认'}</Mono>
            </div>
          ))}
        </div>

        <div style={{ flex: 1 }} />

        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          <button onClick={() => {}} style={{
            background: 'transparent', border: `1px solid ${COL.line3}`, color: COL.t1,
            padding: '10px 0', fontFamily: FONT_DISP, fontSize: 11, letterSpacing: '0.20em', fontWeight: 600,
          }}>SAVE AS PRESET</button>
          <button onClick={onNext} style={{
            background: COL.phos, border: `1px solid ${COL.phos}`, color: COL.bg0,
            padding: '14px 0', fontFamily: FONT_DISP, fontSize: 12, letterSpacing: '0.22em', fontWeight: 700, cursor: 'pointer',
          }}>VALIDATE & PRE-FLIGHT →</button>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { ScenarioBuilder });
