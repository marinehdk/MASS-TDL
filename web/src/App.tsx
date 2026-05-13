import { useState, useEffect } from 'react';
import { ScenarioBuilder } from './screens/ScenarioBuilder';
import { Preflight } from './screens/Preflight';
import { BridgeHMI } from './screens/BridgeHMI';
import { RunReport } from './screens/RunReport';
import { TopChrome } from './screens/shared/TopChrome';
import { FooterHotkeyHints } from './screens/shared/FooterHotkeyHints';

type Screen = 'builder' | 'preflight' | 'bridge' | 'report';

function parseHash(): { screen: Screen; runId?: string } {
  const hash = window.location.hash.replace('#/', '');
  if (hash.startsWith('builder')) return { screen: 'builder' };
  if (hash.startsWith('preflight')) {
    const runId = hash.split('/')[1];
    return { screen: 'preflight', runId };
  }
  if (hash.startsWith('bridge')) {
    const runId = hash.split('/')[1];
    return { screen: 'bridge', runId };
  }
  if (hash.startsWith('report')) {
    const runId = hash.split('/')[1];
    return { screen: 'report', runId };
  }
  return { screen: 'builder' };
}

export default function App() {
  const [route, setRoute] = useState<{ screen: Screen; runId?: string }>(parseHash);

  useEffect(() => {
    const onHashChange = () => setRoute(parseHash());
    window.addEventListener('hashchange', onHashChange);
    return () => window.removeEventListener('hashchange', onHashChange);
  }, []);

  const handleNavigate = (screen: Screen) => {
    window.location.hash = `#/${screen}`;
  };

  return (
    <div style={{
      width: '100vw', height: '100vh', display: 'flex', flexDirection: 'column',
      overflow: 'hidden', background: 'var(--bg-0)',
    }}>
      <TopChrome onNavigate={handleNavigate} />
      <div style={{ flex: 1, overflow: 'hidden', position: 'relative' }}>
        {route.screen === 'builder'   && <ScenarioBuilder />}
        {route.screen === 'preflight' && <Preflight />}
        {route.screen === 'bridge'    && <BridgeHMI />}
        {route.screen === 'report'    && <RunReport />}
      </div>
      <FooterHotkeyHints screen={route.screen} />
    </div>
  );
}
