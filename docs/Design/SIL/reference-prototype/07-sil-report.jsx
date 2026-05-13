// sil-report.jsx — Screen ④: Run Report
// Post-run analytics: scrub timeline, KPI metrics, ASDR ledger, trajectory replay.

const { useState: useState_R, useEffect: useEffect_R, useMemo: useMemo_R, useRef: useRef_R } = React;

// ── Fixture: events on a 600s timeline ──────────────────────────────
const REPORT_EVENTS = [
  { t: 0,   k:'INIT',       sev:'info',  m:'M8', d:'session attached · TRANSIT · D3 SUPERVISED' },
  { t: 25,  k:'T01_DET',    sev:'info',  m:'M2', d:'T01 MARITIME STAR 检测 · 距离 4.8 nm' },
  { t: 38,  k:'CPA_PROJ',   sev:'warn',  m:'M2', d:'T01 CPA 投影 0.18 nm (低于 0.40 nm 阈值)' },
  { t: 47,  k:'SCENE_CHG',  sev:'info',  m:'M8', d:'TRANSIT → COLREG_AVOIDANCE' },
  { t: 49,  k:'COLREG_R14', sev:'info',  m:'M6', d:'分类 = HEAD-ON · 本船 GIVE-WAY · 置信 0.91' },
  { t: 52,  k:'MPC_BRANCH', sev:'info',  m:'M5', d:'BC-MPC 5 分支 · 选 br#2 · STBD +40° · 17.2 kn' },
  { t: 67,  k:'AIS_DROP',   sev:'warn',  m:'M2', d:'T01 AIS dropout 30s · 切雷达跟踪' },
  { t: 93,  k:'AIS_REC',    sev:'info',  m:'M2', d:'T01 AIS 恢复 · 一致性 ✓' },
  { t: 140, k:'CPA_MIN',    sev:'info',  m:'M2', d:'CPA_min @ T01 = 0.52 nm · 通过安全限' },
  { t: 152, k:'SCENE_CHG',  sev:'info',  m:'M8', d:'COLREG_AVOIDANCE → TRANSIT' },
  { t: 218, k:'HB_LOSS',    sev:'crit',  m:'M7', d:'M7 心跳丢失 2 帧 · 强制 ToR' },
  { t: 218, k:'ToR_REQ',    sev:'warn',  m:'M8', d:'ToR 请求 · D3 → D2 · 60s 协议启动' },
  { t: 224, k:'ToR_ACK',    sev:'info',  m:'M8', d:'操作员确认接管 · sat1_display=5.8s · D2' },
  { t: 224, k:'OVERRIDE',   sev:'warn',  m:'M8', d:'OVERRIDE_ACTIVE · M5 冻结 · M7 降级监测' },
  { t: 288, k:'HANDBACK',   sev:'info',  m:'M8', d:'回切序列启动 · M7→M5 · 120ms 完成' },
  { t: 289, k:'TRANSIT',    sev:'info',  m:'M8', d:'OVERRIDE → TRANSIT · D2 → D3' },
  { t: 401, k:'GNSS_BIAS',  sev:'warn',  m:'M3', d:'GNSS 偏置 +12m · EKF 抑制 · 影响可忽略' },
  { t: 480, k:'WP_REACH',   sev:'info',  m:'M4', d:'WP-15 到达 · 误差 38m' },
  { t: 600, k:'END',        sev:'info',  m:'M8', d:'run complete · 评估 PASS · ASDR sealed' },
];

const KPI_METRICS = [
  { k:'CPA_min',           v:'0.52 nm',  th:'≥ 0.40 nm', pass:true,  trend:[0.8,0.7,0.6,0.55,0.52,0.6,0.7,0.85,0.9,0.92,0.95,1.1] },
  { k:'COLREGs 合规率',     v:'1.00',     th:'≥ 0.95',   pass:true,  trend:[1,1,1,1,1,1,1,1,1,1,1,1] },
  { k:'路径偏离 RMS',       v:'0.48 nm',  th:'≤ 1.20 nm',pass:true,  trend:[0.1,0.15,0.3,0.55,0.7,0.85,0.95,0.65,0.5,0.4,0.42,0.48] },
  { k:'ToR 应答时延',       v:'5.8 s',    th:'≤ 35 s',   pass:true,  trend:[] },
  { k:'M5 求解 p95',        v:'24 ms',    th:'≤ 30 ms',  pass:true,  trend:[12,14,18,22,24,21,19,17,16,15,18,22] },
  { k:'M7 心跳丢失',        v:'2 帧 · 注入', th:'no spurious', pass:true,  trend:[] },
  { k:'ASDR 完整性',        v:'SHA-256 ✓',   th:'100%',        pass:true,  trend:[] },
  { k:'ODD 包络偏离',       v:'0 次',     th:'0',         pass:true,  trend:[] },
];

// ── Sparkline helper ───────────────────────────────────────────────
function Sparkline({ data, color = COL.phos, w = 100, h = 24, fill = true }) {
  if (!data || data.length === 0) return <div style={{ width: w, height: h }} />;
  const min = Math.min(...data), max = Math.max(...data);
  const range = max - min || 1;
  const pts = data.map((v, i) => [(i/(data.length-1))*w, h - ((v-min)/range)*h*0.85 - 2]);
  const d = pts.map((p,i) => (i?'L':'M') + p[0].toFixed(1) + ' ' + p[1].toFixed(1)).join(' ');
  const fd = d + ` L ${w} ${h} L 0 ${h} Z`;
  return (
    <svg width={w} height={h}>
      {fill && <path d={fd} fill={color} opacity="0.15" />}
      <path d={d} fill="none" stroke={color} strokeWidth="1.2" />
      <circle cx={pts[pts.length-1][0]} cy={pts[pts.length-1][1]} r="2" fill={color} />
    </svg>
  );
}

// ── Trajectory mini-map (replays ownship + targets through scrub) ─
function TrajectoryMap({ progress }) {
  // simulate own ship path: straight north then jog right then back
  const N = 60;
  const pts = useMemo_R(() => {
    return Array.from({length: N}, (_, i) => {
      const u = i / (N-1);
      const x = 80 + u * 360 + (u > 0.4 && u < 0.7 ? 50 * Math.sin((u-0.4)*Math.PI/0.3) : 0);
      const y = 320 - u * 280;
      return [x, y];
    });
  }, []);
  const visIdx = Math.floor(progress * (N - 1));
  const visPts = pts.slice(0, visIdx + 1);
  const cur = pts[visIdx];

  const t01 = pts.map(([x,y],i) => [x + 120 - i*1.6, y - 80 + i*1.0]);
  const t02 = pts.map(([x,y],i) => [x + 220 + i*0.9, y + 60 - i*0.4]);
  const t01v = t01.slice(0, visIdx + 1);
  const t02v = t02.slice(0, visIdx + 1);

  return (
    <svg viewBox="0 0 480 360" style={{ width: '100%', height: '100%', background: '#050810' }}>
      {/* grid */}
      {[0,1,2,3,4,5].map(i => (
        <line key={'h'+i} x1="0" y1={60*i} x2="480" y2={60*i} stroke={COL.line1} strokeWidth="0.4" />
      ))}
      {[0,1,2,3,4,5,6,7,8].map(i => (
        <line key={'v'+i} x1={60*i} y1="0" x2={60*i} y2="360" stroke={COL.line1} strokeWidth="0.4" />
      ))}
      {/* scale */}
      <text x="6" y="350" fontFamily={FONT_MONO} fontSize="9" fill={COL.t3}>0 nm</text>
      <text x="440" y="350" fontFamily={FONT_MONO} fontSize="9" fill={COL.t3}>6 nm</text>

      {/* planned path */}
      <path d={pts.map(([x,y],i)=>(i?'L':'M')+x+' '+y).join(' ')} stroke={COL.line3} strokeWidth="1" strokeDasharray="3 4" fill="none" />

      {/* targets full ghost paths */}
      <path d={t01.map(([x,y],i)=>(i?'L':'M')+x+' '+y).join(' ')} stroke={COL.danger} strokeWidth="0.6" strokeDasharray="2 3" fill="none" opacity="0.4" />
      <path d={t02.map(([x,y],i)=>(i?'L':'M')+x+' '+y).join(' ')} stroke={COL.warn} strokeWidth="0.6" strokeDasharray="2 3" fill="none" opacity="0.4" />

      {/* visited paths */}
      <path d={visPts.map(([x,y],i)=>(i?'L':'M')+x+' '+y).join(' ')} stroke={COL.phos} strokeWidth="1.5" fill="none" />
      <path d={t01v.map(([x,y],i)=>(i?'L':'M')+x+' '+y).join(' ')} stroke={COL.danger} strokeWidth="1.2" fill="none" />
      <path d={t02v.map(([x,y],i)=>(i?'L':'M')+x+' '+y).join(' ')} stroke={COL.warn} strokeWidth="1.2" fill="none" />

      {/* current positions */}
      {cur && (
        <g>
          <circle cx={cur[0]} cy={cur[1]} r="14" fill="none" stroke={COL.phos} strokeWidth="0.6" opacity="0.4" />
          <g transform={`translate(${cur[0]},${cur[1]}) rotate(0)`}>
            <path d="M 0 -8 L 5 5 L 0 2 L -5 5 Z" fill={COL.phos} />
          </g>
        </g>
      )}
      {t01v.length > 0 && (
        <g transform={`translate(${t01v[t01v.length-1][0]},${t01v[t01v.length-1][1]}) rotate(180)`}>
          <path d="M 0 -7 L 4 4 L 0 1.5 L -4 4 Z" fill="none" stroke={COL.danger} strokeWidth="1.2" />
        </g>
      )}
      {t02v.length > 0 && (
        <g transform={`translate(${t02v[t02v.length-1][0]},${t02v[t02v.length-1][1]}) rotate(220)`}>
          <path d="M 0 -7 L 4 4 L 0 1.5 L -4 4 Z" fill="none" stroke={COL.warn} strokeWidth="1.2" />
        </g>
      )}

      {/* labels */}
      <text x="80" y="335" fontFamily={FONT_DISP} fontSize="10" letterSpacing="0.10em" fill={COL.phos}>OWN</text>
      {t01v.length>0 && <text x={t01v[t01v.length-1][0]+8} y={t01v[t01v.length-1][1]-4} fontFamily={FONT_DISP} fontSize="9" fill={COL.danger}>T01</text>}
      {t02v.length>0 && <text x={t02v[t02v.length-1][0]+8} y={t02v[t02v.length-1][1]-4} fontFamily={FONT_DISP} fontSize="9" fill={COL.warn}>T02</text>}
    </svg>
  );
}

function RunReport({ scenario, onBackToBridge, onNewRun }) {
  const total = 600;
  const [cursor, setCursor] = useState_R(140); // sim seconds
  const [playing, setPlaying] = useState_R(false);
  const [rate, setRate] = useState_R(1);
  const [filter, setFilter] = useState_R('ALL');
  const trackRef = useRef_R(null);

  useEffect_R(() => {
    if (!playing) return;
    const id = setInterval(() => setCursor(c => {
      const n = c + rate;
      if (n >= total) { setPlaying(false); return total; }
      return n;
    }), 200);
    return () => clearInterval(id);
  }, [playing, rate]);

  useEffect_R(() => {
    const onKey = (e) => {
      if (e.key === ' ') { setPlaying(p => !p); e.preventDefault(); }
      if (e.key === 'ArrowLeft')  setCursor(c => Math.max(0, c - 5));
      if (e.key === 'ArrowRight') setCursor(c => Math.min(total, c + 5));
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  const fmtT = t => `T+${String(Math.floor(t/60)).padStart(2,'0')}:${String(t%60).padStart(2,'0')}`;
  const filtered = REPORT_EVENTS.filter(e => filter === 'ALL' || (filter === 'WARN+' ? e.sev !== 'info' : e.sev === filter.toLowerCase()));
  const passedCount = KPI_METRICS.filter(m => m.pass).length;

  // Timeline lanes
  const lanes = ['M2','M5','M6','M7','M8','M3'];
  const laneOf = (m) => Math.max(0, lanes.indexOf(m));

  const onTrackClick = (e) => {
    const r = trackRef.current.getBoundingClientRect();
    setCursor(Math.round(((e.clientX - r.left) / r.width) * total));
  };

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '1.45fr 1fr', height: '100%' }}>
      {/* ── Left column: replay + timeline + KPI grid ── */}
      <div style={{ padding: 18, display: 'flex', flexDirection: 'column', gap: 14, overflow: 'hidden' }}>
        {/* Header */}
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-end' }}>
          <div>
            <Disp size={18} color={COL.t0} weight={700} tracking="0.16em">RUN REPORT · SIL-0427</Disp>
            <Mono size={11} color={COL.t3} style={{ display:'block', marginTop:2 }}>
              {scenario.name} · seed {scenario.seed} · 600s · 2026-05-13 06:42:01 UTC
            </Mono>
          </div>
          <div style={{ display: 'flex', gap: 8 }}>
            <button onClick={onBackToBridge} style={{
              background:'transparent', border:`1px solid ${COL.line3}`, color: COL.t1,
              padding:'8px 14px', fontFamily: FONT_DISP, fontSize: 10.5, letterSpacing:'0.18em', fontWeight: 600, cursor:'pointer',
            }}>← BACK TO BRIDGE</button>
            <button style={{
              background:'transparent', border:`1px solid ${COL.phos}`, color: COL.phos,
              padding:'8px 14px', fontFamily: FONT_DISP, fontSize: 10.5, letterSpacing:'0.18em', fontWeight: 600, cursor:'pointer',
            }}>↓ EXPORT ASDR</button>
            <button onClick={onNewRun} style={{
              background: COL.phos, border:`1px solid ${COL.phos}`, color: COL.bg0,
              padding:'8px 14px', fontFamily: FONT_DISP, fontSize: 10.5, letterSpacing:'0.18em', fontWeight: 700, cursor:'pointer',
            }}>NEW RUN →</button>
          </div>
        </div>

        {/* Overall verdict + KPI tiles */}
        <div style={{ display: 'grid', gridTemplateColumns: '220px 1fr', gap: 12 }}>
          <div style={{ background: COL.bg1, border: `1px solid ${COL.stbd}`, borderLeft: `3px solid ${COL.stbd}`, padding: 14, display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
            <Label color={COL.stbd}>VERDICT</Label>
            <Disp size={42} color={COL.stbd} weight={700} tracking="0.16em">PASS</Disp>
            <Mono size={10} color={COL.t2}>{passedCount}/{KPI_METRICS.length} criteria · 0 fail · 0 warn</Mono>
            <Mono size={9} color={COL.t3} style={{ marginTop: 4, display: 'block' }}>signed · SHA-256 7f3a · sealed 06:52:11Z</Mono>
          </div>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 6 }}>
            {KPI_METRICS.map((m,i) => (
              <div key={i} style={{ background: COL.bg1, border: `1px solid ${COL.line1}`, borderLeft: `2px solid ${m.pass?COL.stbd:COL.danger}`, padding: '8px 10px' }}>
                <Label color={COL.t3}>{m.k}</Label>
                <Mono size={15} color={m.pass?COL.stbd:COL.danger} weight={700}>{m.v}</Mono>
                <Mono size={9} color={COL.t3} style={{ display:'block' }}>{m.th}</Mono>
                {m.trend.length > 0 && (
                  <div style={{ marginTop: 4 }}>
                    <Sparkline data={m.trend} color={m.pass?COL.stbd:COL.danger} w={150} h={20} />
                  </div>
                )}
              </div>
            ))}
          </div>
        </div>

        {/* Trajectory + transport */}
        <div style={{ background: COL.bg1, border: `1px solid ${COL.line1}`, padding: 12, display: 'flex', flexDirection: 'column', gap: 8, flex: 1, minHeight: 0 }}>
          <div style={{ display:'flex', justifyContent:'space-between', alignItems:'baseline' }}>
            <Label color={COL.phos}>TRAJECTORY REPLAY · {fmtT(cursor)}</Label>
            <Mono size={9.5} color={COL.t3}>own · T01 (give-way) · T02 (stand-on) · 全程 RMS 0.48 nm</Mono>
          </div>
          <div style={{ flex: 1, minHeight: 0 }}>
            <TrajectoryMap progress={cursor / total} />
          </div>

          {/* Transport bar */}
          <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
            <button onClick={() => setPlaying(p => !p)} style={{
              background: playing ? COL.warn : COL.phos, border:'none', color: COL.bg0,
              padding:'7px 14px', fontFamily: FONT_DISP, fontSize: 11, letterSpacing:'0.18em', fontWeight: 700, cursor:'pointer',
            }}>{playing ? '❚❚' : '▶'}</button>
            <Mono size={11} color={COL.t0} weight={600}>{fmtT(cursor)}</Mono>
            <Mono size={10} color={COL.t3}>/ {fmtT(total)}</Mono>
            <Seg value={rate} onChange={setRate} options={[{v:0.5,l:'½×'},{v:1,l:'1×'},{v:2,l:'2×'},{v:5,l:'5×'},{v:10,l:'10×'}]} />
            <div style={{ flex: 1 }} />
            <Mono size={10} color={COL.t3}>← → 跳 5s · SPACE 播放</Mono>
          </div>
        </div>

        {/* Multi-lane timeline scrubber */}
        <div style={{ background: COL.bg1, border: `1px solid ${COL.line1}`, padding: 12 }}>
          <div style={{ display:'flex', justifyContent:'space-between', alignItems:'baseline', marginBottom: 8 }}>
            <Label color={COL.t3}>EVENT TIMELINE · M2/M3/M5/M6/M7/M8</Label>
            <Mono size={10} color={COL.t2}>{REPORT_EVENTS.length} events · click track to scrub</Mono>
          </div>
          <div ref={trackRef} onClick={onTrackClick} style={{ position: 'relative', height: 110, background: COL.bg0, border: `1px solid ${COL.line1}`, cursor: 'pointer' }}>
            {/* lane labels */}
            {lanes.map((m, i) => (
              <div key={m} style={{ position:'absolute', left: 4, top: 4 + i*16, fontFamily: FONT_DISP, fontSize: 8.5, color: COL.t3, letterSpacing:'0.14em' }}>{m}</div>
            ))}
            {/* lane lines */}
            {lanes.map((_, i) => (
              <div key={i} style={{ position:'absolute', left: 30, right: 4, top: 8 + i*16, height: 1, background: COL.line1 }} />
            ))}
            {/* events */}
            {REPORT_EVENTS.map((e, i) => {
              const xPct = (e.t / total) * 100;
              const li = laneOf(e.m);
              const c = e.sev === 'crit' ? COL.danger : e.sev === 'warn' ? COL.warn : COL.info;
              return (
                <div key={i} title={`${fmtT(e.t)} · ${e.k} · ${e.d}`} style={{
                  position:'absolute', left: `calc(30px + (100% - 34px) * ${xPct/100})`, top: 4 + li*16,
                  width: 4, height: 10, background: c, marginLeft: -2,
                }} />
              );
            })}
            {/* time axis */}
            <div style={{ position:'absolute', bottom: 0, left: 30, right: 4, height: 14, borderTop: `1px solid ${COL.line2}` }}>
              {[0,1,2,3,4,5,6].map(i => (
                <div key={i} style={{ position:'absolute', left: `${(i/6)*100}%`, top: 0, fontFamily: FONT_MONO, fontSize: 8.5, color: COL.t3, transform: 'translateX(-50%)' }}>
                  T+{(i*100).toString().padStart(2,'0')}s
                </div>
              ))}
            </div>
            {/* cursor */}
            <div style={{
              position:'absolute', top: 0, bottom: 0, left: `calc(30px + (100% - 34px) * ${(cursor/total)})`,
              width: 1.5, background: COL.phos, boxShadow: `0 0 6px ${COL.phos}`, pointerEvents: 'none',
            }}>
              <div style={{ position:'absolute', top: -3, left: -3, width: 7, height: 7, background: COL.phos, transform: 'rotate(45deg)' }} />
            </div>
          </div>
        </div>
      </div>

      {/* ── Right column: ASDR ledger ── */}
      <div style={{ background: COL.bg1, borderLeft: `1px solid ${COL.line2}`, display: 'flex', flexDirection: 'column' }}>
        <div style={{ padding: 14, borderBottom: `1px solid ${COL.line1}` }}>
          <div style={{ display:'flex', justifyContent:'space-between', alignItems:'baseline' }}>
            <Disp size={13} color={COL.phos} tracking="0.18em" weight={700}>ASDR LEDGER</Disp>
            <Mono size={9.5} color={COL.t3}>14.3 MB · {REPORT_EVENTS.length} events · SHA-256 ✓</Mono>
          </div>
          <div style={{ marginTop: 10, display: 'flex', gap: 6 }}>
            {['ALL','INFO','WARN+','CRIT'].map(f => (
              <div key={f} onClick={() => setFilter(f)} style={{
                padding: '4px 10px', cursor: 'pointer',
                background: filter === f ? COL.phos : 'transparent',
                color: filter === f ? COL.bg0 : COL.t2,
                border: `1px solid ${filter === f ? COL.phos : COL.line2}`,
                fontFamily: FONT_DISP, fontSize: 10, letterSpacing: '0.16em', fontWeight: 600,
              }}>{f}</div>
            ))}
          </div>
        </div>

        <div style={{ flex: 1, overflowY: 'auto', padding: '8px 14px' }}>
          {filtered.map((e, i) => {
            const c = e.sev === 'crit' ? COL.danger : e.sev === 'warn' ? COL.warn : COL.info;
            const isCur = Math.abs(e.t - cursor) < 6;
            return (
              <div key={i} onClick={() => setCursor(e.t)} style={{
                display: 'grid', gridTemplateColumns: '60px 38px 116px 1fr', gap: 8, alignItems: 'baseline',
                padding: '7px 8px', cursor: 'pointer',
                borderLeft: `2px solid ${c}`,
                background: isCur ? `${COL.phos}14` : (i % 2 ? COL.bg0 : 'transparent'),
                marginBottom: 2,
              }}>
                <Mono size={10} color={COL.t3}>{fmtT(e.t)}</Mono>
                <Disp size={10} color={c} weight={700} tracking="0.10em">{e.m}</Disp>
                <Mono size={10} color={c} weight={600}>{e.k}</Mono>
                <span style={{ fontFamily: FONT_BODY, fontSize: 10.5, color: COL.t1, lineHeight: 1.4 }}>{e.d}</span>
              </div>
            );
          })}
        </div>

        <div style={{ padding: 14, borderTop: `1px solid ${COL.line1}` }}>
          <Label color={COL.t3}>SHA-256 CHAIN · last record</Label>
          <Mono size={9.5} color={COL.t1} style={{ display:'block', marginTop: 4, wordBreak:'break-all', lineHeight: 1.5 }}>
            7f3a · 9c2b · 14d8 · 6ee0 · b811 · 5a3c · f0e9 · 2d44
            <br />→ 1bc2.4a8e.f30d.9912.0b7a.66e4.cc91.5d28
          </Mono>
          <div style={{ marginTop: 10, display: 'flex', gap: 8 }}>
            <button style={{ flex:1, background: 'transparent', border: `1px solid ${COL.line3}`, color: COL.t1, padding: '8px 0', fontFamily: FONT_DISP, fontSize: 10, letterSpacing:'0.18em', fontWeight: 600, cursor:'pointer' }}>VERIFY CHAIN</button>
            <button style={{ flex:1, background: 'transparent', border: `1px solid ${COL.phos}`, color: COL.phos, padding: '8px 0', fontFamily: FONT_DISP, fontSize: 10, letterSpacing:'0.18em', fontWeight: 600, cursor:'pointer' }}>↓ EXPORT JSON</button>
          </div>
        </div>
      </div>
    </div>
  );
}

// Small seg-control reused (local copy because cross-file scope)
function Seg({ value, onChange, options }) {
  return (
    <div style={{ display: 'inline-flex', border: `1px solid ${COL.line2}` }}>
      {options.map(o => {
        const sel = value === o.v;
        return (
          <div key={o.v} onClick={() => onChange(o.v)} style={{
            padding:'4px 9px', cursor:'pointer',
            background: sel ? COL.phos : 'transparent',
            color: sel ? COL.bg0 : COL.t2,
            fontFamily: FONT_DISP, fontSize: 9.5, letterSpacing:'0.16em', fontWeight: 600,
            borderRight: `1px solid ${COL.line1}`,
            textTransform: 'uppercase',
          }}>{o.l}</div>
        );
      })}
    </div>
  );
}

Object.assign(window, { RunReport });
