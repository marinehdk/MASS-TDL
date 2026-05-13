// sil-bridge.jsx — Screen ③: Bridge HMI (live)
// Layout-C derivative. Live sim clock; ToR modal; fault injection; MRC.

const { useState: useState_BR, useEffect: useEffect_BR, useMemo: useMemo_BR } = React;

// ── Fault catalogue (matches builder pre-scheduled list) ───────────
const FAULT_CATALOG = [
  { id:'AIS_DROP',    m:'M2', sev:'warn', name:'AIS dropout (T01)',     dur: 30 },
  { id:'HB_LOSS',     m:'M7', sev:'crit', name:'M7 heartbeat 2-frame',  dur: 8 },
  { id:'GNSS_BIAS',   m:'M3', sev:'warn', name:'GNSS bias +12m',        dur: 25 },
  { id:'SOLVE_TO',    m:'M5', sev:'warn', name:'M5 solve timeout',      dur: 12 },
  { id:'INTENT_FLIP', m:'M2', sev:'warn', name:'T02 intent change',     dur: 0  },
  { id:'RADAR_RAIN',  m:'M2', sev:'warn', name:'Radar rain clutter',    dur: 40 },
];

// ── Reusable HUD pod ───────────────────────────────────────────────
function HudPod({ children, style, accent = COL.line2, glass = true }) {
  return (
    <div style={{
      background: glass ? 'rgba(7,12,19,0.82)' : COL.bg1,
      backdropFilter: glass ? 'blur(6px)' : 'none',
      border: `1px solid ${accent}`,
      ...style,
    }}>{children}</div>
  );
}

// ── Pulse-clock TMR/TDL ─────────────────────────────────────────────
function TmrTdl({ tmr, tdl, tdl0 = 240 }) {
  const tdlColor = tdl < 30 ? COL.danger : tdl < 60 ? COL.warn : COL.stbd;
  const pct = Math.max(0, Math.min(1, tdl / tdl0));
  return (
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8, marginTop: 8 }}>
      <div>
        <Label color={COL.warn}>TMR</Label>
        <Mono size={20} color={COL.warn} weight={700}>{tmr}<Mono size={11} color={COL.t3}> s</Mono></Mono>
      </div>
      <div>
        <Label color={tdlColor}>TDL</Label>
        <Mono size={20} color={tdlColor} weight={700}>{tdl}<Mono size={11} color={COL.t3}> s</Mono></Mono>
      </div>
      <div style={{ gridColumn: 'span 2', height: 4, background: COL.bg2, position: 'relative' }}>
        <div style={{ position: 'absolute', inset: 0, width: `${pct*100}%`, background: tdlColor, transition: 'width 0.4s' }} />
        <div style={{ position: 'absolute', left: `${(tmr/tdl0)*100}%`, top: -2, bottom: -2, width: 1.5, background: COL.warn }} />
      </div>
    </div>
  );
}

// ── Threat row with hover focus ─────────────────────────────────────
function ThreatRow({ t }) {
  return (
    <div style={{
      padding: 10, background: 'rgba(7,12,19,0.85)', backdropFilter: 'blur(6px)',
      borderLeft: `2px solid ${t.c}`, border: `1px solid ${t.c}55`, cursor: 'pointer',
    }}>
      <div style={{ display:'flex', justifyContent:'space-between' }}>
        <Disp size={11} color={COL.t0} tracking="0.10em" weight={500}>{t.n}</Disp>
        <Mono size={9} color={COL.t3}>BRG {t.brg}</Mono>
      </div>
      <Mono size={9.5} color={t.c} style={{ display:'block', marginTop: 2 }}>{t.r}</Mono>
      <div style={{ display:'flex', justifyContent:'space-between', marginTop: 4 }}>
        <Mono size={11} color={t.c} weight={600}>{t.cpa}</Mono>
        <Mono size={11} color={t.c} weight={600}>{t.tcpa}</Mono>
      </div>
    </div>
  );
}

// ── ToR Modal ───────────────────────────────────────────────────────
function ToRModal({ active, reason, deadline, sat1Held, onAccept, onDecline, onClose }) {
  if (!active) return null;
  const canAccept = sat1Held >= 5;
  return (
    <div style={{
      position: 'absolute', inset: 0, background: 'rgba(7,12,19,0.78)', backdropFilter: 'blur(6px)',
      display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 30,
    }}>
      <div style={{ width: 620, border: `2px solid ${COL.warn}`, background: COL.bg1, position: 'relative' }}>
        {/* Top scan light */}
        <div style={{ height: 4, background: COL.bg0, position: 'relative', overflow: 'hidden' }}>
          <div style={{ position: 'absolute', inset: 0, width: '30%', background: `linear-gradient(90deg, transparent, ${COL.warn}, transparent)`, animation: 'scan-line 1.6s linear infinite' }} />
        </div>
        <div style={{ padding: 22 }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
            <Disp size={11} color={COL.warn} weight={700} tracking="0.22em">REQUEST TO TAKEOVER · F-NEW-004</Disp>
            <Mono size={10} color={COL.t3}>D3 → D2</Mono>
          </div>
          <div style={{ marginTop: 12, fontFamily: FONT_DISP, fontSize: 18, color: COL.t0, fontWeight: 500, lineHeight: 1.4 }}>
            {reason}
          </div>
          {/* 2x2 context grid */}
          <div style={{ marginTop: 16, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
            <div style={{ padding: 10, background: COL.bg0, borderLeft: `2px solid ${COL.danger}` }}>
              <Label color={COL.danger}>NEAREST CPA</Label>
              <Mono size={18} color={COL.danger} weight={700}>0.20 nm · 3'12"</Mono>
              <Mono size={10} color={COL.t3} style={{ display:'block' }}>T01 · MARITIME STAR · GIVE-WAY</Mono>
            </div>
            <div style={{ padding: 10, background: COL.bg0, borderLeft: `2px solid ${COL.warn}` }}>
              <Label color={COL.warn}>RECOMMENDED</Label>
              <Mono size={18} color={COL.warn} weight={700}>STBD +40° · 17.2 kn</Mono>
              <Mono size={10} color={COL.t3} style={{ display:'block' }}>M5 · BC-MPC br#2 · ε 0.04</Mono>
            </div>
            <div style={{ padding: 10, background: COL.bg0, borderLeft: `2px solid ${COL.info}` }}>
              <Label color={COL.info}>FALLBACK</Label>
              <Mono size={18} color={COL.info} weight={700}>MRC · DRIFT</Mono>
              <Mono size={10} color={COL.t3} style={{ display:'block' }}>等待新 ODD 评估</Mono>
            </div>
            <div style={{ padding: 10, background: COL.bg0, borderLeft: `2px solid ${COL.phos}` }}>
              <Label color={COL.phos}>AUTH LEVEL</Label>
              <Disp size={16} color={COL.phos} weight={700} tracking="0.16em">D2 · MANUAL (RO)</Disp>
              <Mono size={10} color={COL.t3} style={{ display:'block' }}>operator · capt.wei</Mono>
            </div>
          </div>

          {/* Countdown + SAT-1 lock */}
          <div style={{ marginTop: 16, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16, alignItems: 'center' }}>
            <div>
              <Label color={COL.t3}>DEADLINE</Label>
              <Mono size={36} color={deadline < 15 ? COL.danger : COL.warn} weight={700}>
                {String(deadline).padStart(2,'0')}<Mono size={14} color={COL.t3}> s</Mono>
              </Mono>
              <div style={{ height: 4, background: COL.bg0, marginTop: 4 }}>
                <div style={{ width: `${(deadline/60)*100}%`, height: '100%', background: deadline < 15 ? COL.danger : COL.warn, transition: 'width 0.6s linear' }} />
              </div>
            </div>
            <div>
              <Label color={COL.t3}>SAT-1 SETTLEMENT</Label>
              <Mono size={36} color={canAccept ? COL.stbd : COL.t2} weight={700}>
                {Math.min(5, sat1Held).toFixed(1)}<Mono size={14} color={COL.t3}> / 5.0 s</Mono>
              </Mono>
              <div style={{ height: 4, background: COL.bg0, marginTop: 4 }}>
                <div style={{ width: `${Math.min(100,(sat1Held/5)*100)}%`, height: '100%', background: canAccept ? COL.stbd : COL.t3, transition: 'width 0.2s linear' }} />
              </div>
            </div>
          </div>

          <div style={{ marginTop: 18, display: 'flex', gap: 10 }}>
            <button onClick={onDecline} style={{
              flex: 1, background: 'transparent', border: `1px solid ${COL.line3}`, color: COL.t1,
              padding: '14px 0', fontFamily: FONT_DISP, fontSize: 11.5, letterSpacing: '0.20em', fontWeight: 700, cursor: 'pointer',
            }}>DECLINE · 维持 D3</button>
            <button disabled={!canAccept} onClick={onAccept} style={{
              flex: 2, background: canAccept ? COL.warn : COL.bg0,
              border: `1px solid ${canAccept ? COL.warn : COL.line2}`,
              color: canAccept ? COL.bg0 : COL.t3,
              padding: '14px 0', fontFamily: FONT_DISP, fontSize: 12, letterSpacing: '0.20em', fontWeight: 700,
              cursor: canAccept ? 'pointer' : 'not-allowed',
            }}>{canAccept ? 'ACCEPT · 我接管 (D2)' : `ACCEPT · 请先确认当前态势 (${(5 - sat1Held).toFixed(1)}s 后解锁)`}</button>
          </div>
          <Mono size={9.5} color={COL.t3} style={{ display:'block', marginTop: 10 }}>
            ASDR 将记录：sat1_display_duration_s · threats_visible · odd_zone · operator_id · SHA-256
          </Mono>
        </div>
      </div>
    </div>
  );
}

// ── Fault Inject Panel (floating) ───────────────────────────────────
function FaultPanel({ open, onClose, onInject, activeFaults }) {
  if (!open) return null;
  return (
    <HudPod accent={COL.danger} style={{
      position: 'absolute', right: 20, bottom: 130, width: 340, padding: 14, zIndex: 20,
      borderLeft: `3px solid ${COL.danger}`,
    }}>
      <div style={{ display:'flex', justifyContent:'space-between', alignItems:'baseline', marginBottom: 10 }}>
        <Disp size={11.5} color={COL.danger} tracking="0.20em" weight={700}>FAULT INJECTION · DEV</Disp>
        <Mono size={10} color={COL.t3} style={{cursor:'pointer'}} onClick={onClose}>✕</Mono>
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        {FAULT_CATALOG.map(f => {
          const isActive = activeFaults.has(f.id);
          const c = f.sev === 'crit' ? COL.danger : COL.warn;
          return (
            <div key={f.id} onClick={() => onInject(f)} style={{
              display:'grid', gridTemplateColumns:'30px 40px 1fr 40px', gap: 8, alignItems:'center',
              padding:'7px 10px', cursor:'pointer',
              background: isActive ? `${c}22` : COL.bg0,
              border: `1px solid ${isActive ? c : COL.line1}`,
              borderLeft: `2px solid ${c}`,
            }}>
              <Disp size={10} color={c} weight={700} tracking="0.12em">{f.m}</Disp>
              <Disp size={9.5} color={c} weight={700} tracking="0.14em">{f.sev.toUpperCase()}</Disp>
              <Mono size={10.5} color={COL.t1}>{f.name}</Mono>
              <Mono size={9} color={isActive?c:COL.t3}>{isActive?'ON':f.dur?`${f.dur}s`:'one'}</Mono>
            </div>
          );
        })}
      </div>
      <Mono size={9.5} color={COL.t3} style={{ display:'block', marginTop: 8 }}>
        点击注入 · M7 心跳丢失将自动触发 ToR · 所有事件写入 ASDR
      </Mono>
    </HudPod>
  );
}

// ── Main Bridge HMI ─────────────────────────────────────────────────
function BridgeHMI({ scenario, runState, setRunState, simT, setSimT, onFinish }) {
  // Scene state machine driven by sim time + manual triggers
  const [scene, setScene] = useState_BR('TRANSIT'); // TRANSIT / COLREG_AVOIDANCE / TOR / OVERRIDE / MRC / HANDBACK
  const [showFaults, setShowFaults] = useState_BR(false);
  const [activeFaults, setActiveFaults] = useState_BR(new Set());
  const [faultLog, setFaultLog] = useState_BR([]);
  const [torDeadline, setTorDeadline] = useState_BR(60);
  const [torStart, setTorStart] = useState_BR(null);
  const [sat1Held, setSat1Held] = useState_BR(0);
  const [autoLvl, setAutoLvl] = useState_BR('D3');
  const [handbackStep, setHandbackStep] = useState_BR(-1);

  // Auto scene transition at T+25s into avoidance (demo arc)
  useEffect_BR(() => {
    if (scene === 'TRANSIT' && simT >= 25) setScene('COLREG_AVOIDANCE');
  }, [simT, scene]);

  // ToR timer
  useEffect_BR(() => {
    if (scene !== 'TOR' || runState !== 'RUN') return;
    const id = setInterval(() => {
      setTorDeadline(d => Math.max(0, d - 1));
      setSat1Held(h => h + 1);
    }, 1000);
    return () => clearInterval(id);
  }, [scene, runState]);

  // Auto MRC on tor timeout
  useEffect_BR(() => {
    if (scene === 'TOR' && torDeadline === 0) {
      setFaultLog(l => [...l, [simT, 'ToR_TIMEOUT', 'crit', '60s 无应答 · 自动 MRC · DRIFT']]);
      setScene('MRC');
      setAutoLvl('MRC');
    }
  }, [torDeadline, scene, simT]);

  // Handback progress ticker
  useEffect_BR(() => {
    if (scene !== 'HANDBACK') return;
    const id = setInterval(() => setHandbackStep(s => s < 4 ? s + 1 : s), 600);
    return () => clearInterval(id);
  }, [scene]);
  useEffect_BR(() => {
    if (scene === 'HANDBACK' && handbackStep >= 4) {
      const t = setTimeout(() => { setScene('TRANSIT'); setAutoLvl('D3'); setHandbackStep(-1); }, 1000);
      return () => clearTimeout(t);
    }
  }, [handbackStep, scene]);

  const triggerToR = (why = 'TDL ≤ TMR · 决策时窗已紧迫 · 请船长确认接管') => {
    setScene('TOR');
    setTorStart(simT);
    setTorDeadline(60);
    setSat1Held(0);
    setFaultLog(l => [...l, [simT, 'ToR_REQUEST', 'warn', why]]);
  };
  const acceptToR = () => {
    setFaultLog(l => [...l, [simT, 'ToR_ACK', 'info', `sat1_display=${sat1Held.toFixed(1)}s · accept · D2`]]);
    setScene('OVERRIDE');
    setAutoLvl('D2');
  };
  const declineToR = () => {
    setFaultLog(l => [...l, [simT, 'ToR_DECLINE', 'warn', '操作员拒绝接管 · 维持 D3']]);
    setScene('COLREG_AVOIDANCE');
  };
  const requestHandback = () => {
    setFaultLog(l => [...l, [simT, 'HANDBACK_REQ', 'info', 'M7 → M5 顺序回切启动']]);
    setScene('HANDBACK');
    setHandbackStep(0);
  };

  const injectFault = (f) => {
    setActiveFaults(s => {
      const n = new Set(s); n.add(f.id); return n;
    });
    setFaultLog(l => [...l, [simT, f.id, f.sev, f.name]]);
    if (f.id === 'HB_LOSS') {
      setTimeout(() => triggerToR('M7 心跳丢失 ≥2 帧 · 安全监督失效 · 强制 ToR'), 200);
    }
    if (f.dur) {
      setTimeout(() => setActiveFaults(s => { const n = new Set(s); n.delete(f.id); return n; }), f.dur * 200);
    }
  };

  // Keyboard hotkeys
  useEffect_BR(() => {
    const onKey = (e) => {
      if (e.key === ' ') { setRunState(r => r === 'RUN' ? 'PAUSE' : 'RUN'); e.preventDefault(); }
      if (e.key === 't' || e.key === 'T') triggerToR();
      if (e.key === 'f' || e.key === 'F') setShowFaults(v => !v);
      if (e.key === 'm' || e.key === 'M') { setScene('MRC'); setAutoLvl('MRC'); }
      if (e.key === 'h' || e.key === 'H') requestHandback();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  const tmr = 60;
  const tdl = Math.max(30, 240 - simT * 2);
  const sceneColor = scene === 'TOR' ? COL.danger
                  : scene === 'OVERRIDE' ? COL.warn
                  : scene === 'MRC' ? COL.danger
                  : scene === 'COLREG_AVOIDANCE' ? COL.warn
                  : scene === 'HANDBACK' ? COL.info
                  : COL.phos;
  const sceneLabel = {
    TRANSIT:'TRANSIT', COLREG_AVOIDANCE:'COLREG_AVOIDANCE',
    TOR:'TOR_REQUESTED', OVERRIDE:'OVERRIDE_ACTIVE', MRC:'MRC_ACTIVE', HANDBACK:'HANDBACK',
  }[scene];

  const threats = useMemo_BR(() => [
    { n:'T01 · MARITIME STAR', r:'GIVE-WAY · R14', cpa:'0.20 nm', tcpa:'3\'12"', brg:'358°', c: COL.danger },
    { n:'T02 · NORDLAND',      r:'STAND-ON · R15', cpa:'1.80 nm', tcpa:'9\'30"', brg:'042°', c: COL.warn },
    { n:'T03 · FERRY OBI',     r:'SAFE',           cpa:'2.60 nm', tcpa:'14\'10"', brg:'285°', c: COL.info },
  ], []);

  // SVG chart sized for display, scales via viewBox
  return (
    <div style={{ width: '100%', height: '100%', position: 'relative', background: '#0a1628', overflow: 'hidden' }}>
      {/* Chart base */}
      <div style={{ position: 'absolute', inset: 0 }}>
        <ChartBg width={1920} height={920} scene={scene.toLowerCase()} />
      </div>
      <svg style={{ position:'absolute', inset:0, width:'100%', height:'100%' }} viewBox="0 0 1920 920" preserveAspectRatio="xMidYMid slice">
        {/* Planned path */}
        <path d="M 800 820 L 880 700 L 1000 580 L 1180 460 L 1400 360"
          stroke={scene === 'MRC' ? COL.danger : COL.stbd} strokeWidth="1.5" fill="none" opacity={scene === 'OVERRIDE' ? 0.4 : 0.9} />
        {[[880,700],[1000,580],[1180,460],[1400,360]].map((p,i)=>(
          <g key={i} opacity={scene==='OVERRIDE'?0.4:1}>
            <circle cx={p[0]} cy={p[1]} r="4" fill={COL.stbd} />
            <text x={p[0]+8} y={p[1]+3} fontFamily={FONT_MONO} fontSize="10" fill={COL.stbd}>WP-{12+i}</text>
          </g>
        ))}

        {/* MRC drift line if MRC */}
        {scene === 'MRC' && (
          <g>
            <path d="M 800 820 L 760 720 L 700 640 L 620 580" stroke={COL.stbd} strokeWidth="2" strokeDasharray="8 6" fill="none" />
            <text x="610" y="572" fontFamily={FONT_DISP} fontSize="11" letterSpacing="0.10em" fill={COL.stbd}>MRC · DRIFT</text>
          </g>
        )}

        {/* Targets */}
        <Target x={800} y={400} heading={180} role="give-way" label="T01 · MARITIME STAR" cpa="0.20" tcpa={"3'12\""} showAPF={scene !== 'TRANSIT' && scene !== 'MRC'} />
        <Target x={1280} y={680} heading={290} role="stand-on" label="T02 · NORDLAND" cpa="1.8" tcpa={"9'30\""} showAPF={scene === 'COLREG_AVOIDANCE'} />
        <Target x={400} y={300} heading={70} role="safe" label="T03 · FERRY OBI" cpa="2.6" tcpa={"14'10\""} />
        {/* Ghost track only when planning is live */}
        {scene !== 'OVERRIDE' && scene !== 'MRC' && (
          <GhostTrack pts={[[800,820],[850,720],[920,640],[1010,580]]} />
        )}
        {/* Own ship */}
        <OwnShip x={800} y={820} heading={scene === 'COLREG_AVOIDANCE' || scene === 'TOR' ? 18 : scene === 'MRC' ? -25 : 5} scale={1.4} />
        {/* CPA ring on closest target */}
        <circle cx="800" cy="600" r="180" fill="none" stroke={COL.warn} strokeWidth="0.8" strokeDasharray="4 6" opacity="0.4" />
      </svg>

      {/* ── Top-left HUD: vessel + autonomy ── */}
      <HudPod accent={COL.line2} style={{ position:'absolute', top:14, left:14, padding:'10px 14px', display:'flex', gap:14, alignItems:'center' }}>
        <div style={{ width:32, height:32, border:`1.5px solid ${COL.phos}`, position:'relative' }}>
          <div style={{ position:'absolute', left:'50%', top:'50%', width:1, height:14, background: COL.phos, transformOrigin:'bottom', animation:'radar-sweep 4s linear infinite' }} />
        </div>
        <div>
          <Disp size={11} color={COL.phos} tracking="0.22em" weight={700}>FCB-45M · OWNSHIP</Disp>
          <Mono size={11} color={COL.t0} weight={600}>IMO 9876543 · {scenario.name}</Mono>
        </div>
        <Sep vertical color={COL.line1} />
        <AutoBadge level={autoLvl} label={
          autoLvl === 'D2' ? 'MANUAL (RO)' :
          autoLvl === 'D3' ? 'SUPERVISED' :
          autoLvl === 'MRC' ? 'DRIFT' : 'FULL AUTO'
        } state={scene === 'TOR' ? 'critical' : 'normal'} />
      </HudPod>

      {/* ── Top-right HUD: scene + TMR/TDL ── */}
      <HudPod accent={sceneColor} style={{ position:'absolute', top:14, right:14, padding:'10px 14px', minWidth:300, borderLeft:`3px solid ${sceneColor}` }}>
        <div style={{ display:'flex', justifyContent:'space-between', alignItems:'baseline' }}>
          <Disp size={12} color={sceneColor} tracking="0.18em" weight={700}>SCENE · {sceneLabel}</Disp>
          <Mono size={10} color={COL.t3}>
            {scene === 'COLREG_AVOIDANCE' ? 'RULE 14 · HEAD-ON' :
             scene === 'TOR'              ? 'TDL ≤ TMR' :
             scene === 'OVERRIDE'         ? 'M7 DEG.MON' :
             scene === 'MRC'              ? 'MRC-01' :
             scene === 'HANDBACK'         ? 'M7→M5 ↻' : 'NOMINAL'}
          </Mono>
        </div>
        <TmrTdl tmr={tmr} tdl={tdl} />
        <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 6 }}>
          <Mono size={9.5} color={COL.t3}>ODD A · conf 0.{scene === 'MRC' ? '62' : '87'}</Mono>
          <Mono size={9.5} color={scene==='MRC'?COL.warn:COL.t3}>{scene === 'MRC' ? 'EDGE' : 'IN-ENVELOPE'}</Mono>
        </div>
      </HudPod>

      {/* ── SAT-2 drawer (expanded during avoidance / override) ── */}
      {(scene === 'COLREG_AVOIDANCE' || scene === 'OVERRIDE') && (
        <HudPod accent={COL.warn} style={{ position:'absolute', left:14, top:130, width: 380, padding: 14, borderLeft:`3px solid ${COL.warn}` }}>
          <div style={{ display:'flex', justifyContent:'space-between', marginBottom: 8 }}>
            <Disp size={12} color={COL.warn} tracking="0.18em" weight={700}>SAT-2 · 决策依据</Disp>
            <Mono size={9} color={COL.t3}>auto-expand</Mono>
          </div>
          {scene === 'COLREG_AVOIDANCE' ? (
            <div style={{ fontFamily: FONT_DISP, fontSize: 16, color: COL.t0, lineHeight: 1.4, fontWeight: 500 }}>
              正前方 <span style={{color:COL.warn}}>对遇</span> Maritime Star，向 <span style={{color:COL.stbd}}>右舷 40°</span> 让路 (Rule 14)
            </div>
          ) : (
            <div style={{ fontFamily: FONT_DISP, fontSize: 16, color: COL.t0, lineHeight: 1.4, fontWeight: 500 }}>
              <span style={{color:COL.warn}}>接管模式</span> · M5 已冻结 · 自动 M7 降级监测
            </div>
          )}
          <div style={{ marginTop: 10, padding: '8px 10px', background: COL.bg0, borderLeft: `2px solid ${COL.phos}` }}>
            <Mono size={9.5} color={COL.t3}>{scene === 'OVERRIDE' ? 'M7 监测' : '规则链'}</Mono>
            <div style={{ fontSize: 11, color: COL.t1, marginTop: 3, lineHeight: 1.6 }}>
              {scene === 'OVERRIDE' ? (
                <>RTT 127ms · 心跳 ✓ · 新威胁 0 · M7 wd_timer 198ms</>
              ) : (
                <>对遇 ±6° → <Mono color={COL.warn}>R14</Mono> → 互让 → <Mono color={COL.warn}>R8</Mono> 大幅 ≥30° → <Mono color={COL.stbd}>STBD +40°</Mono></>
              )}
            </div>
          </div>
        </HudPod>
      )}

      {/* ── Handback progress ── */}
      {scene === 'HANDBACK' && (
        <HudPod accent={COL.info} style={{ position:'absolute', left:14, top:130, width: 380, padding: 14, borderLeft:`3px solid ${COL.info}` }}>
          <Disp size={12} color={COL.info} tracking="0.18em" weight={700}>HANDBACK · M7 → M5 顺序回切</Disp>
          <div style={{ marginTop: 12, display: 'flex', flexDirection: 'column', gap: 6 }}>
            {[
              ['T+0ms',   'M1 进入回切准备'],
              ['T+10ms',  'M7 启动'],
              ['T+100ms', 'M7 心跳确认 (READY)'],
              ['T+110ms', 'M5 恢复规划'],
              ['T+120ms', '首个 NORMAL 避碰计划'],
            ].map((s,i) => {
              const done = handbackStep >= i;
              return (
                <div key={i} style={{ display:'grid', gridTemplateColumns:'14px 70px 1fr', gap: 10, alignItems:'center' }}>
                  <div style={{ width: 12, height: 12, borderRadius:'50%', border: `1.5px solid ${done?COL.stbd:COL.t3}`, background: done?COL.stbd:'transparent' }} />
                  <Mono size={10} color={done?COL.stbd:COL.t3}>{s[0]}</Mono>
                  <Mono size={10.5} color={done?COL.t0:COL.t2}>{s[1]}</Mono>
                </div>
              );
            })}
          </div>
        </HudPod>
      )}

      {/* ── Threat ribbon ── */}
      <div style={{ position:'absolute', right: 14, top: 160, width: 280, display:'flex', flexDirection:'column', gap: 8 }}>
        <Label color={COL.danger}>威胁队列 · {threats.length} ACTIVE</Label>
        {threats.map((t,i) => <ThreatRow key={i} t={t} />)}
      </div>

      {/* ── Bottom conning strip ── */}
      <div style={{ position:'absolute', bottom: 14, left: '50%', transform:'translateX(-50%)' }}>
        <HudPod accent={COL.line2} style={{ padding:'8px 16px', display:'flex' }}>
          <KPI label="HDG"    value={scene==='MRC'?'335':scene==='COLREG_AVOIDANCE'||scene==='TOR'?'018':'005'} unit="°T" sub={scene==='COLREG_AVOIDANCE'?'CMD 040°':'CMD 005°'} accent={scene==='COLREG_AVOIDANCE'?COL.warn:COL.t0} />
          <KPI label="SOG"    value={scene==='MRC'?'2.4':'18.4'} unit="kn" sub={scene==='MRC'?'drift':'17.2 cmd'} accent={scene==='MRC'?COL.danger:COL.t0} />
          <KPI label="COG"    value="008" unit="°T" sub="set 002°" />
          <KPI label="ROT"    value={scene==='COLREG_AVOIDANCE'?'+6.4':'+0.2'} unit="°/s" sub={scene==='COLREG_AVOIDANCE'?'向右旋回':'稳定'} accent={scene==='COLREG_AVOIDANCE'?COL.stbd:COL.t0} />
          <KPI label="RUDDER" value={scene==='COLREG_AVOIDANCE'?'12':'02'} unit="°S" sub="auto" accent={scene==='COLREG_AVOIDANCE'?COL.stbd:COL.t0} />
          <KPI label="ROC RTT" value="127" unit="ms" sub="LTE ok" accent={COL.stbd} />
        </HudPod>
      </div>

      {/* ── Operator action rail (left edge) ── */}
      <div style={{ position:'absolute', left: 14, bottom: 14, display:'flex', flexDirection:'column', gap: 6 }}>
        <button onClick={() => triggerToR()} style={{
          background: 'rgba(7,12,19,0.85)', backdropFilter:'blur(6px)',
          border: `1px solid ${COL.warn}`, borderLeft: `3px solid ${COL.warn}`,
          color: COL.warn, padding: '10px 16px',
          fontFamily: FONT_DISP, fontSize: 11, letterSpacing: '0.18em', fontWeight: 700, cursor: 'pointer',
          textAlign: 'left',
        }}>⚑ REQUEST TAKEOVER<br/><span style={{fontSize:9, color:COL.t3, letterSpacing:'0.10em'}}>D3 → D2 · 60s 协议 · [T]</span></button>
        <button onClick={() => { setScene('MRC'); setAutoLvl('MRC'); setFaultLog(l=>[...l,[simT,'MANUAL_MRC','crit','船长触发 MRC · DRIFT']]); }} style={{
          background: 'rgba(7,12,19,0.85)', backdropFilter:'blur(6px)',
          border: `1px solid ${COL.danger}`, borderLeft: `3px solid ${COL.danger}`,
          color: COL.danger, padding: '10px 16px',
          fontFamily: FONT_DISP, fontSize: 11, letterSpacing: '0.18em', fontWeight: 700, cursor: 'pointer',
          textAlign: 'left',
        }}>● MRC · DRIFT<br/><span style={{fontSize:9, color:COL.t3, letterSpacing:'0.10em'}}>最低风险 · [M]</span></button>
        {scene === 'OVERRIDE' && (
          <button onClick={requestHandback} style={{
            background: 'rgba(7,12,19,0.85)', backdropFilter:'blur(6px)',
            border: `1px solid ${COL.info}`, borderLeft: `3px solid ${COL.info}`,
            color: COL.info, padding: '10px 16px',
            fontFamily: FONT_DISP, fontSize: 11, letterSpacing: '0.18em', fontWeight: 700, cursor: 'pointer',
            textAlign: 'left',
          }}>↺ RETURN TO AUTO<br/><span style={{fontSize:9, color:COL.t3, letterSpacing:'0.10em'}}>D2 → D3 · [H]</span></button>
        )}
        <button onClick={() => setShowFaults(v => !v)} style={{
          background: 'rgba(7,12,19,0.85)', backdropFilter:'blur(6px)',
          border: `1px solid ${COL.danger}`, color: COL.danger, padding: '8px 14px',
          fontFamily: FONT_DISP, fontSize: 10, letterSpacing: '0.16em', fontWeight: 700, cursor: 'pointer',
          textAlign: 'left',
        }}>⚡ FAULT INJECT · [F]</button>
      </div>

      {/* ── Sim controls (top-right above status) ── */}
      <HudPod accent={COL.line2} style={{ position:'absolute', top: 110, right: 14, padding:'8px 12px', display:'flex', gap: 10, alignItems:'center' }}>
        <button onClick={() => setRunState(r => r === 'RUN' ? 'PAUSE' : 'RUN')} style={{
          background: runState === 'RUN' ? COL.warn : COL.stbd,
          border: 'none', color: COL.bg0, padding: '6px 12px',
          fontFamily: FONT_DISP, fontSize: 10, letterSpacing: '0.18em', fontWeight: 700, cursor: 'pointer',
        }}>{runState === 'RUN' ? '❚❚ PAUSE' : '▶ RESUME'}</button>
        <Mono size={10} color={COL.t3}>[SPACE]</Mono>
        <Sep vertical color={COL.line1} />
        <button onClick={onFinish} style={{
          background: 'transparent', border: `1px solid ${COL.line3}`, color: COL.t1,
          padding: '6px 12px', fontFamily: FONT_DISP, fontSize: 10, letterSpacing: '0.18em', fontWeight: 600, cursor: 'pointer',
        }}>END · REPORT →</button>
      </HudPod>

      {/* ── Event log (bottom-right, small) ── */}
      <HudPod accent={COL.line2} style={{ position:'absolute', right: 14, bottom: 130, width: 280, padding: 10 }}>
        <Label color={COL.t3}>EVENT LOG · ASDR</Label>
        <div style={{ marginTop: 6, maxHeight: 120, overflowY: 'auto', fontFamily: FONT_MONO, fontSize: 10, lineHeight: 1.6 }}>
          {faultLog.length === 0 ? (
            <div style={{ color: COL.t3 }}>— no events recorded —</div>
          ) : faultLog.slice().reverse().map((e,i)=>{
            const c = e[2]==='crit'?COL.danger:e[2]==='warn'?COL.warn:COL.info;
            return (
              <div key={i} style={{ display:'grid', gridTemplateColumns:'46px 84px 1fr', gap: 6, borderBottom:`1px dashed ${COL.line1}`, padding:'3px 0' }}>
                <span style={{ color: COL.t3 }}>T+{String(Math.floor(e[0]/60)).padStart(2,'0')}:{String(e[0]%60).padStart(2,'0')}</span>
                <span style={{ color: c, fontWeight: 600 }}>{e[1]}</span>
                <span style={{ color: COL.t2 }}>{e[3]}</span>
              </div>
            );
          })}
        </div>
      </HudPod>

      <FaultPanel open={showFaults} onClose={() => setShowFaults(false)} onInject={injectFault} activeFaults={activeFaults} />

      <ToRModal
        active={scene === 'TOR'}
        reason={torStart !== null ? 'TDL ≤ TMR · 系统判断决策时窗已不充裕，请船长确认接管' : ''}
        deadline={torDeadline}
        sat1Held={sat1Held}
        onAccept={acceptToR}
        onDecline={declineToR}
      />

      {/* MRC banner */}
      {scene === 'MRC' && (
        <div style={{
          position:'absolute', top: 0, left: 0, right: 0, height: 6,
          background: COL.danger, animation: 'warn-flash 0.6s infinite',
        }} />
      )}
    </div>
  );
}

Object.assign(window, { BridgeHMI });
