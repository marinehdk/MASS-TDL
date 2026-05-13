// sil-app.jsx — SIL Simulator: top-level routing + chrome
// Loaded LAST. Owns screen state, top chrome, status pills, and run lifecycle.

const { useState, useEffect, useMemo, useRef } = React;

// ── Shared status bar pill ──────────────────────────────────────────
function StatusPill({ dot, label, value, color = COL.t2, mono = true, blink = false }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '0 14px', borderRight: `1px solid ${COL.line1}`, height: '100%' }}>
      {dot && <div style={{
        width: 7, height: 7, borderRadius: '50%', background: color,
        boxShadow: `0 0 6px ${color}`,
        animation: blink ? 'phos-pulse 1.2s infinite' : 'phos-pulse 2.4s infinite',
      }} />}
      <span style={{ fontFamily: FONT_DISP, fontSize: 9.5, letterSpacing: '0.20em', color: COL.t3, textTransform: 'uppercase' }}>{label}</span>
      {value !== undefined && (
        mono
          ? <Mono size={11} color={color} weight={600}>{value}</Mono>
          : <Disp size={11} color={color} weight={600} tracking="0.14em">{value}</Disp>
      )}
    </div>
  );
}

// ── Top chrome: SIL simulator brand + screen tabs ──────────────────
function TopBar({ screen, setScreen, runState, sessionId, simClock, utcClock, scenarioName }) {
  const tabs = [
    { id: 'builder',  code: '01', label: 'SCENARIO',  zh: '场景编辑' },
    { id: 'preflight',code: '02', label: 'PRE-FLIGHT',zh: '飞行前检查' },
    { id: 'bridge',   code: '03', label: 'BRIDGE',    zh: '驾驶台运行' },
    { id: 'report',   code: '04', label: 'REPORT',    zh: '回放与报告' },
  ];
  const runColor = runState === 'RUN' ? COL.stbd : runState === 'PAUSE' ? COL.warn : runState === 'FAULT' ? COL.danger : COL.t3;

  return (
    <div style={{
      height: 56, background: COL.bg1, borderBottom: `1px solid ${COL.line2}`,
      display: 'flex', alignItems: 'stretch', flexShrink: 0,
    }}>
      {/* Brand block */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '0 18px', borderRight: `1px solid ${COL.line2}`, minWidth: 260 }}>
        <div style={{ width: 28, height: 28, border: `1.5px solid ${COL.phos}`, position: 'relative', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <div style={{ width: 1, height: 12, background: COL.phos, transformOrigin: 'bottom center', position: 'absolute', bottom: '50%', animation: 'radar-sweep 4s linear infinite' }} />
          <div style={{ width: 3, height: 3, background: COL.phos, borderRadius: '50%' }} />
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', lineHeight: 1.15 }}>
          <Disp size={12} color={COL.phos} tracking="0.20em" weight={700}>COLAV · SIL</Disp>
          <Mono size={9.5} color={COL.t3}>MASS L3 · TDL v1.1.2 · Session {sessionId}</Mono>
        </div>
      </div>

      {/* Screen tabs */}
      <div style={{ display: 'flex', alignItems: 'stretch' }}>
        {tabs.map((t, i) => {
          const active = screen === t.id;
          return (
            <div key={t.id} onClick={() => setScreen(t.id)} style={{
              display: 'flex', alignItems: 'center', gap: 10, padding: '0 18px', cursor: 'pointer',
              borderRight: `1px solid ${COL.line1}`,
              background: active ? COL.bg0 : 'transparent',
              borderBottom: active ? `2px solid ${COL.phos}` : '2px solid transparent',
              marginBottom: -1,
            }}>
              <Mono size={10} color={active ? COL.phos : COL.t3}>{t.code}</Mono>
              <div style={{ display: 'flex', flexDirection: 'column', lineHeight: 1.15 }}>
                <Disp size={11.5} color={active ? COL.t0 : COL.t2} tracking="0.16em" weight={600}>{t.label}</Disp>
                <span style={{ fontFamily: FONT_BODY, fontSize: 9.5, color: COL.t3 }}>{t.zh}</span>
              </div>
            </div>
          );
        })}
      </div>

      <div style={{ flex: 1 }} />

      {/* Status pills */}
      <div style={{ display: 'flex', alignItems: 'center', height: '100%' }}>
        {scenarioName && <StatusPill label="Scenario" value={scenarioName} color={COL.t1} mono={false} />}
        <StatusPill dot label="SIM" value={simClock} color={runColor} blink={runState === 'FAULT'} />
        <StatusPill label="UTC" value={utcClock} color={COL.t1} />
        <StatusPill label="State" value={runState} color={runColor} mono={false} />
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '0 18px' }}>
          <div style={{ width: 8, height: 8, borderRadius: '50%', background: COL.danger, animation: 'phos-pulse 2s infinite' }} />
          <Disp size={10} color={COL.danger} tracking="0.18em" weight={600}>REC</Disp>
        </div>
      </div>
    </div>
  );
}

// ── Footer: keyboard hints + connection ────────────────────────────
function FooterBar({ screen }) {
  const hints = {
    builder:   [['1/2/3', '切换三个 Tab'], ['CLICK', '选择 Imazu 案例'], ['⌘ S', '保存场景'], ['→', '进入 Pre-flight']],
    preflight: [['SPACE', '运行所有检查'], ['R', '重试失败项'], ['→', '启动 Bridge']],
    bridge:    [['SPACE', '暂停/继续'], ['T', '触发 ToR'], ['F', '注入故障'], ['M', 'MRC'], ['→', '结束 → Report']],
    report:    [['◀ ▶', '逐帧'], ['SPACE', '播放/暂停'], ['E', '导出 ASDR'], ['←', '回到 Bridge']],
  };
  return (
    <div style={{
      height: 28, background: COL.bg1, borderTop: `1px solid ${COL.line1}`,
      display: 'flex', alignItems: 'center', padding: '0 14px', gap: 18, flexShrink: 0,
    }}>
      <Mono size={9.5} color={COL.phos}>● WS://m8.local:7820  RTT 18ms</Mono>
      <span style={{ width: 1, height: 14, background: COL.line2 }} />
      <Mono size={9.5} color={COL.t3}>ASDR /var/asdr/2026-05-13T0642.log · 14.3 MB · SHA-256 ✓</Mono>
      <div style={{ flex: 1 }} />
      {(hints[screen] || []).map(([k, v], i) => (
        <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <span style={{
            fontFamily: FONT_MONO, fontSize: 9.5, color: COL.t1, fontWeight: 600,
            border: `1px solid ${COL.line2}`, padding: '1px 5px', background: COL.bg0,
          }}>{k}</span>
          <span style={{ fontFamily: FONT_BODY, fontSize: 9.5, color: COL.t3 }}>{v}</span>
        </div>
      ))}
    </div>
  );
}

function App() {
  const [screen, setScreen] = useState('builder');
  const [runState, setRunState] = useState('IDLE'); // IDLE / READY / RUN / PAUSE / FAULT / DONE
  const [simT, setSimT] = useState(0);  // simulated seconds since run start
  const [utcT, setUtcT] = useState(() => new Date());
  const [scenario, setScenario] = useState({
    name: 'IM03_Crossing_GiveWay_v2',
    imazuId: 3,
    seed: 20260513,
    duration: 600,
  });

  // UTC ticks once per second
  useEffect(() => {
    const id = setInterval(() => setUtcT(new Date()), 1000);
    return () => clearInterval(id);
  }, []);

  // Sim clock — advances only when running
  useEffect(() => {
    if (runState !== 'RUN') return;
    const id = setInterval(() => setSimT(t => t + 1), 1000);
    return () => clearInterval(id);
  }, [runState]);

  // Auto-start sim when entering Bridge if READY
  useEffect(() => {
    if (screen === 'bridge' && runState === 'READY') setRunState('RUN');
    if (screen === 'report' && runState === 'RUN') setRunState('DONE');
  }, [screen]);

  const simClock = useMemo(() => {
    const m = Math.floor(simT / 60).toString().padStart(2, '0');
    const s = (simT % 60).toString().padStart(2, '0');
    return `T+${m}:${s}`;
  }, [simT]);
  const utcClock = useMemo(() => {
    const z = n => n.toString().padStart(2, '0');
    return `${z(utcT.getUTCHours())}:${z(utcT.getUTCMinutes())}:${z(utcT.getUTCSeconds())}Z`;
  }, [utcT]);

  const goBuilder    = () => setScreen('builder');
  const goPreflight  = () => { setScreen('preflight'); setRunState('IDLE'); };
  const goBridge     = () => { setRunState('READY'); setScreen('bridge'); setSimT(0); };
  const goReport     = () => { setScreen('report'); setRunState('DONE'); };

  return (
    <div style={{
      width: '100vw', height: '100vh', background: COL.bg0, color: COL.t1,
      display: 'flex', flexDirection: 'column', overflow: 'hidden',
      fontFamily: FONT_BODY,
    }}>
      <TopBar
        screen={screen} setScreen={setScreen}
        runState={runState}
        sessionId="SIL-0427"
        simClock={simClock}
        utcClock={utcClock}
        scenarioName={scenario.name}
      />

      <div style={{ flex: 1, position: 'relative', overflow: 'hidden' }}>
        {screen === 'builder'   && <ScenarioBuilder  scenario={scenario} setScenario={setScenario} onNext={goPreflight} />}
        {screen === 'preflight' && <Preflight        scenario={scenario} onBack={goBuilder} onNext={goBridge} />}
        {screen === 'bridge'    && <BridgeHMI        scenario={scenario} runState={runState} setRunState={setRunState} simT={simT} setSimT={setSimT} onFinish={goReport} />}
        {screen === 'report'    && <RunReport        scenario={scenario} onBackToBridge={() => setScreen('bridge')} onNewRun={goBuilder} />}
      </div>

      <FooterBar screen={screen} />
    </div>
  );
}

ReactDOM.createRoot(document.getElementById('root')).render(<App />);
