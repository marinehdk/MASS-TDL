// sil-preflight.jsx — Screen ②: Pre-flight Check
// Walks through M1..M8 + sensors + ASDR + comm-link health before authorizing Bridge.

const { useState: useState_P, useEffect: useEffect_P } = React;

const MODULE_LIST = [
  { id:'M1', name:'ODD State / TMR / TDL',  src:'sango_adas/m1',  hz:1,
    checks:[
      ['配置加载',       'ok',  'odd_zones.yaml v1.1.2 · 4 zones'],
      ['TMR baseline',  'ok',  'TMR0 = 60s · TDL0 = 240s'],
      ['envelope eval', 'ok',  'in-envelope · conf 0.94'],
    ]},
  { id:'M2', name:'WorldState (AIS · Radar 融合)', src:'sango_adas/m2', hz:10,
    checks:[
      ['AIS 接入',       'ok',   '3 contacts @ T-30s'],
      ['Radar 接入',     'ok',   '12 nm range · 1.4 deg az'],
      ['融合一致性',     'warn', '低对比目标 T03 仅雷达可见 (置信 0.78)'],
    ]},
  { id:'M3', name:'Localization (GNSS · IMU)', src:'sango_adas/m3', hz:20,
    checks:[
      ['GNSS RTK',       'ok',  'fix=FLOAT · σ 1.8m'],
      ['IMU 偏置',       'ok',  'bias gyro 0.02°/s'],
      ['EKF 协方差',     'ok',  'P_pos = 2.1 m²'],
    ]},
  { id:'M4', name:'IvP Decision (Behavior Lib)', src:'sango_adas/m4', hz:2,
    checks:[
      ['行为库注册',     'ok',  '7 behaviors · COLREGS_AVOID @ 0.71'],
      ['IvP solve',     'ok',  'solve_time 4.2 ms'],
      ['权重一致性',     'ok',  'Σ weights = 1.0000'],
    ]},
  { id:'M5', name:'Mid-MPC Planner',           src:'sango_adas/m5', hz:2,
    checks:[
      ['BC-MPC init',   'ok',  'N=60 · dt=1s · branches=5'],
      ['warm-start',    'ok',  '前序解可用'],
      ['solver health', 'ok',  'IPOPT · last 8.3 ms'],
    ]},
  { id:'M6', name:'COLREGs Reasoning',         src:'sango_adas/m6', hz:2,
    checks:[
      ['规则库加载',     'ok',  'R8/R9/R13/R14/R15/R17/R19'],
      ['分类置信度',     'ok',  '所有目标 ≥ 0.82'],
      ['给定 role 一致', 'ok',  'GW=1 · SO=0 · safe=0'],
    ]},
  { id:'M7', name:'Safety Supervisor',         src:'sango_adas/m7', hz:50,
    checks:[
      ['Doer-Checker',  'ok',  'heartbeat 50Hz · 0 miss'],
      ['SOTIF triggers','ok',  '0 alerts'],
      ['Watchdog',      'ok',  'wd_timer 198 ms'],
    ]},
  { id:'M8', name:'TDL Orchestrator / UI Bus', src:'sango_adas/m8', hz:50,
    checks:[
      ['Scene FSM',     'ok',  'TRANSIT · ready for events'],
      ['UI_State emit', 'ok',  '50 Hz · ws://m8.local:7820'],
      ['ASDR writer',   'ok',  'SHA-chain healthy'],
    ]},
];

const SENSOR_LIST = [
  { id:'GNSS-A', name:'Trimble BD992', stat:'ok',   meta:'fix FLOAT · 22 sat · HDOP 0.6' },
  { id:'GNSS-B', name:'Septentrio mosaic', stat:'ok', meta:'fix FIXED · 18 sat · backup' },
  { id:'IMU',    name:'Xsens MTi-680G', stat:'ok',  meta:'200Hz · bias compensated' },
  { id:'RADAR',  name:'JRC JMR-9230',   stat:'ok',  meta:'12 nm · 2D · CFAR on' },
  { id:'AIS',    name:'Furuno FA-170',  stat:'ok',  meta:'class A · 3 in-range' },
  { id:'LIDAR',  name:'Velodyne VLS-128', stat:'warn', meta:'1 channel dropout (ch07)' },
  { id:'CAM-FWD',name:'FLIR M364C',     stat:'ok',  meta:'EO/IR · 30fps · cal ok' },
  { id:'CAM-AFT',name:'Axis Q1798',     stat:'ok',  meta:'EO · 25fps · cal ok' },
];

function StatusDot({ stat, size = 8 }) {
  const c = stat === 'ok' ? COL.stbd : stat === 'warn' ? COL.warn : stat === 'fail' ? COL.danger : COL.t3;
  return <div style={{ width: size, height: size, borderRadius: '50%', background: c, boxShadow: `0 0 ${size}px ${c}` }} />;
}
function StatusBadge({ stat }) {
  const map = { ok:[COL.stbd,'OK'], warn:[COL.warn,'WARN'], fail:[COL.danger,'FAIL'], wait:[COL.t3,'WAIT'], run:[COL.info,'RUN'] };
  const [c,lbl] = map[stat] || map.wait;
  return (
    <div style={{ display:'inline-flex', alignItems:'center', gap:6, padding:'2px 8px', border:`1px solid ${c}`, background:`${c}1a` }}>
      <div style={{ width:5, height:5, borderRadius:'50%', background:c, animation: stat==='run'?'phos-pulse 1s infinite':'none' }} />
      <Disp size={9.5} color={c} weight={700} tracking="0.16em">{lbl}</Disp>
    </div>
  );
}

function ModuleCard({ mod, expanded, onToggle, progress }) {
  const overall = mod.checks.some(c=>c[1]==='fail') ? 'fail'
                : mod.checks.some(c=>c[1]==='warn') ? 'warn' : 'ok';
  const accent = overall === 'fail' ? COL.danger : overall === 'warn' ? COL.warn : COL.stbd;
  return (
    <div style={{ background: COL.bg1, border: `1px solid ${COL.line1}`, borderLeft: `2px solid ${accent}` }}>
      <div onClick={onToggle} style={{ padding: '10px 14px', cursor: 'pointer', display: 'grid', gridTemplateColumns: '40px 1fr 90px 90px', alignItems: 'center', gap: 12 }}>
        <Disp size={14} color={accent} weight={700} tracking="0.10em">{mod.id}</Disp>
        <div>
          <Disp size={12} color={COL.t0} weight={600} tracking="0.10em">{mod.name}</Disp>
          <Mono size={9.5} color={COL.t3} style={{ display:'block', marginTop: 1 }}>{mod.src} · {mod.hz}Hz · {mod.checks.length} checks</Mono>
        </div>
        <Mono size={11} color={COL.t1}>{progress < 100 ? `${progress}%` : ''}</Mono>
        <StatusBadge stat={progress < 100 ? 'run' : overall} />
      </div>
      {/* Progress bar */}
      <div style={{ height: 2, background: COL.bg0, position: 'relative' }}>
        <div style={{ position:'absolute', inset:0, width: `${progress}%`, background: accent, transition: 'width 0.4s' }} />
      </div>
      {expanded && (
        <div style={{ padding: '8px 14px 12px', background: COL.bg0 }}>
          {mod.checks.map((c,i)=>(
            <div key={i} style={{ display: 'grid', gridTemplateColumns: '20px 180px 1fr 70px', alignItems: 'center', gap: 8, padding: '4px 0', borderBottom: i<mod.checks.length-1?`1px dashed ${COL.line1}`:'none' }}>
              <StatusDot stat={c[1]} size={6} />
              <Mono size={10.5} color={COL.t1}>{c[0]}</Mono>
              <Mono size={10} color={COL.t2}>{c[2]}</Mono>
              <Mono size={10} color={c[1]==='ok'?COL.stbd:c[1]==='warn'?COL.warn:COL.danger} weight={600}>{c[1].toUpperCase()}</Mono>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

function Preflight({ scenario, onBack, onNext }) {
  const [progress, setProgress] = useState_P(() => MODULE_LIST.map(()=>0));
  const [running, setRunning] = useState_P(false);
  const [expanded, setExpanded] = useState_P('M5');
  const [logLines, setLogLines] = useState_P([
    '[06:42:01.014] preflight.runner: session SIL-0427 attached to scenario',
    '[06:42:01.015] preflight.runner: 8 modules · 8 sensors · 6 link checks queued',
  ]);

  // Animate "running" pass — fills bars in sequence
  useEffect_P(() => {
    if (!running) return;
    let i = 0;
    const id = setInterval(() => {
      setProgress(p => {
        const nxt = [...p];
        if (i < MODULE_LIST.length) {
          nxt[i] = Math.min(100, (nxt[i] || 0) + 18);
          if (nxt[i] >= 100) {
            const m = MODULE_LIST[i];
            setLogLines(l => [...l, `[06:42:${(2+i).toString().padStart(2,'0')}.${(112+i*73).toString().slice(-3)}] ${m.id.toLowerCase()}.checker: ${m.checks.length}/${m.checks.length} passed (${m.id==='M2'?'1 warn':'clean'})`]);
            i++;
          }
        } else {
          clearInterval(id);
          setRunning(false);
        }
        return nxt;
      });
    }, 140);
    return () => clearInterval(id);
  }, [running]);

  const allDone = progress.every(p => p >= 100);
  const anyWarn = MODULE_LIST.some((m,i) => progress[i] >= 100 && m.checks.some(c=>c[1]==='warn'));
  const okCount = MODULE_LIST.filter((m,i) => progress[i] >= 100 && !m.checks.some(c=>c[1]!=='ok')).length;
  const totalChecks = MODULE_LIST.reduce((s,m)=>s+m.checks.length,0);
  const passedChecks = MODULE_LIST.reduce((s,m,i)=> s + (progress[i] >= 100 ? m.checks.filter(c=>c[1]==='ok').length : 0), 0);

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 380px', height: '100%' }}>
      {/* Left: checks */}
      <div style={{ padding: 18, overflowY: 'auto', display: 'flex', flexDirection: 'column', gap: 16 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-end' }}>
          <div>
            <Disp size={18} color={COL.t0} weight={700} tracking="0.16em">PRE-FLIGHT CHECK</Disp>
            <Mono size={11} color={COL.t3} style={{ display: 'block', marginTop: 2 }}>
              scenario · {scenario.name} · checking module readiness against TDL v1.1.2 baseline
            </Mono>
          </div>
          <div style={{ display:'flex', gap: 10 }}>
            <button disabled={running} onClick={() => { setProgress(MODULE_LIST.map(()=>0)); setRunning(true); }} style={{
              background: 'transparent', border: `1px solid ${COL.line3}`, color: COL.t1,
              padding: '9px 18px', fontFamily: FONT_DISP, fontSize: 11, letterSpacing: '0.20em', fontWeight: 600, cursor:'pointer', opacity: running ? 0.5 : 1,
            }}>{allDone ? 'RE-RUN' : running ? 'RUNNING…' : 'RUN CHECKS'}</button>
            <button disabled={!allDone || running} onClick={onNext} style={{
              background: allDone ? COL.phos : 'transparent', border: `1px solid ${allDone?COL.phos:COL.line2}`,
              color: allDone ? COL.bg0 : COL.t3,
              padding: '9px 18px', fontFamily: FONT_DISP, fontSize: 11, letterSpacing: '0.20em', fontWeight: 700, cursor: allDone?'pointer':'not-allowed',
            }}>AUTHORIZE → BRIDGE</button>
          </div>
        </div>

        {/* Modules */}
        <div>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', marginBottom: 8 }}>
            <Label color={COL.t3}>SOFTWARE MODULES · M1–M8</Label>
            <Mono size={10} color={COL.t2}>{passedChecks}/{totalChecks} checks · {okCount}/{MODULE_LIST.length} modules clean</Mono>
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            {MODULE_LIST.map((m, i) => (
              <ModuleCard key={m.id} mod={m}
                expanded={expanded === m.id}
                onToggle={() => setExpanded(expanded === m.id ? null : m.id)}
                progress={progress[i]} />
            ))}
          </div>
        </div>

        {/* Sensors */}
        <div>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', marginBottom: 8 }}>
            <Label color={COL.t3}>SENSORS · CALIBRATION & LIVE</Label>
            <Mono size={10} color={COL.t2}>{SENSOR_LIST.filter(s=>s.stat==='ok').length}/{SENSOR_LIST.length} online</Mono>
          </div>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 6 }}>
            {SENSOR_LIST.map(s => (
              <div key={s.id} style={{ display: 'grid', gridTemplateColumns: '20px 60px 1fr 70px', gap: 8, alignItems: 'center', background: COL.bg1, border: `1px solid ${COL.line1}`, borderLeft: `2px solid ${s.stat==='ok'?COL.stbd:s.stat==='warn'?COL.warn:COL.danger}`, padding: '8px 10px' }}>
                <StatusDot stat={s.stat} />
                <Disp size={10.5} color={COL.t0} weight={600} tracking="0.10em">{s.id}</Disp>
                <div>
                  <Mono size={10.5} color={COL.t1}>{s.name}</Mono>
                  <Mono size={9} color={COL.t3} style={{ display:'block' }}>{s.meta}</Mono>
                </div>
                <StatusBadge stat={s.stat} />
              </div>
            ))}
          </div>
        </div>

        {/* Comm-links */}
        <div>
          <Label color={COL.t3} style={{ marginBottom: 8 }}>COMMUNICATION LINKS</Label>
          <div style={{ background: COL.bg1, border: `1px solid ${COL.line1}` }}>
            {[
              ['M8 ↔ Bridge HMI',     'WS · ws://m8.local:7820',     '18 ms',  COL.stbd, 'ok'],
              ['M8 ↔ ROC (LTE)',      'gRPC · roc.sango-mass.com',   '127 ms', COL.stbd, 'ok'],
              ['M8 ↔ ROC (Iridium)',  'CSD fallback · idle',         '780 ms', COL.warn, 'warn'],
              ['ASDR writer',         '/var/asdr/2026-05-13T0642.log','— ',    COL.stbd, 'ok'],
              ['M7 ↔ Watchdog HW',    'CAN bus · 500 kbps',           '< 1 ms',COL.stbd, 'ok'],
              ['VTS feed (Coastal)',  'NMEA0183 over UDP',           '42 ms',  COL.stbd, 'ok'],
            ].map((r,i)=>(
              <div key={i} style={{ display:'grid', gridTemplateColumns:'20px 1fr 1fr 80px 70px', padding:'8px 12px', alignItems:'center', gap: 10, borderBottom: i<5?`1px solid ${COL.line1}`:'none' }}>
                <StatusDot stat={r[4]} size={6} />
                <Mono size={10.5} color={COL.t1}>{r[0]}</Mono>
                <Mono size={10} color={COL.t2}>{r[1]}</Mono>
                <Mono size={11} color={r[3]} weight={600}>{r[2]}</Mono>
                <StatusBadge stat={r[4]} />
              </div>
            ))}
          </div>
        </div>
      </div>

      {/* Right: live log + readiness gate */}
      <div style={{ background: COL.bg1, borderLeft: `1px solid ${COL.line2}`, display: 'flex', flexDirection: 'column' }}>
        <div style={{ padding: 16, borderBottom: `1px solid ${COL.line1}` }}>
          <Label color={COL.phos}>READINESS GATE</Label>
          <div style={{ marginTop: 12, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
            <div style={{ padding: 10, background: COL.bg0, borderLeft: `2px solid ${allDone?COL.stbd:COL.t3}` }}>
              <Label color={COL.t3}>MODULES</Label>
              <Mono size={22} color={allDone?COL.stbd:COL.t1} weight={700}>{okCount + (anyWarn?MODULE_LIST.filter((m,i)=>progress[i]>=100 && m.checks.some(c=>c[1]==='warn')).length:0)}<Mono size={10} color={COL.t3}> / 8</Mono></Mono>
              <Mono size={9.5} color={COL.t3} style={{ display:'block' }}>{allDone ? `${anyWarn?'1 warn · ':''}ok` : '检查中…'}</Mono>
            </div>
            <div style={{ padding: 10, background: COL.bg0, borderLeft: `2px solid ${COL.stbd}` }}>
              <Label color={COL.t3}>SENSORS</Label>
              <Mono size={22} color={COL.stbd} weight={700}>7<Mono size={10} color={COL.t3}> / 8</Mono></Mono>
              <Mono size={9.5} color={COL.warn} style={{ display:'block' }}>LIDAR ch07 弱信号</Mono>
            </div>
            <div style={{ padding: 10, background: COL.bg0, borderLeft: `2px solid ${COL.stbd}` }}>
              <Label color={COL.t3}>LINKS</Label>
              <Mono size={22} color={COL.stbd} weight={700}>5<Mono size={10} color={COL.t3}> / 6</Mono></Mono>
              <Mono size={9.5} color={COL.warn} style={{ display:'block' }}>Iridium backup 高延迟</Mono>
            </div>
            <div style={{ padding: 10, background: COL.bg0, borderLeft: `2px solid ${COL.phos}` }}>
              <Label color={COL.t3}>ASDR</Label>
              <Mono size={22} color={COL.phos} weight={700}>READY</Mono>
              <Mono size={9.5} color={COL.t3} style={{ display:'block' }}>chain ok · 0 bytes</Mono>
            </div>
          </div>

          <div style={{ marginTop: 14, padding: 10, background: COL.bg0, border: `1px solid ${allDone?COL.stbd:COL.line2}` }}>
            <Label color={allDone?COL.stbd:COL.t3}>OVERALL</Label>
            <Disp size={22} color={allDone?(anyWarn?COL.warn:COL.stbd):COL.t2} weight={700} tracking="0.16em">
              {allDone ? (anyWarn ? 'GO · WITH NOTES' : 'GO') : (running ? 'CHECKING…' : 'PENDING')}
            </Disp>
            <Mono size={9.5} color={COL.t3} style={{ display:'block', marginTop: 4 }}>
              船长授权 · 时间戳将写入 ASDR · ToR 协议生效
            </Mono>
          </div>
        </div>

        {/* Live log */}
        <div style={{ flex: 1, overflowY: 'auto', padding: 12 }}>
          <Label color={COL.t3} style={{ marginBottom: 8 }}>LIVE LOG · checker.stderr</Label>
          <div style={{ fontFamily: FONT_MONO, fontSize: 10, lineHeight: 1.55, color: COL.t2 }}>
            {logLines.map((l,i)=>{
              const isWarn = l.includes('warn');
              const isErr  = l.includes('fail') || l.includes('FAIL');
              return <div key={i} style={{ color: isErr?COL.danger:isWarn?COL.warn:COL.t2, marginBottom: 2 }}>{l}</div>;
            })}
            {running && <div style={{ color: COL.phos }}>▌</div>}
          </div>
        </div>

        <div style={{ padding: 12, borderTop: `1px solid ${COL.line1}`, display: 'flex', gap: 8 }}>
          <button onClick={onBack} style={{
            flex: 1, background: 'transparent', border: `1px solid ${COL.line3}`, color: COL.t1,
            padding: '10px 0', fontFamily: FONT_DISP, fontSize: 10.5, letterSpacing: '0.20em', fontWeight: 600, cursor:'pointer',
          }}>← SCENARIO</button>
          <button disabled={!allDone} onClick={onNext} style={{
            flex: 2, background: allDone ? COL.phos : 'transparent', border: `1px solid ${allDone?COL.phos:COL.line2}`,
            color: allDone ? COL.bg0 : COL.t3,
            padding: '10px 0', fontFamily: FONT_DISP, fontSize: 11, letterSpacing: '0.20em', fontWeight: 700, cursor: allDone?'pointer':'not-allowed',
          }}>START BRIDGE RUN →</button>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { Preflight });
