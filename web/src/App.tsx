import { useState, useEffect } from 'react';
import { SimulationScenario } from './screens/SimulationScenario';
import { SimulationCheck } from './screens/SimulationCheck';
import { SimulationMonitor } from './screens/SimulationMonitor';
import { SimulationEvaluator } from './screens/SimulationEvaluator';
import { TopChrome } from './screens/shared/TopChrome';
import { FooterHotkeyHints } from './screens/shared/FooterHotkeyHints';

type Screen = 'scenario' | 'check' | 'monitor' | 'evaluator';

function parseHash(): { screen: Screen; runId?: string } {
  const hash = window.location.hash.replace('#/', '');
  if (hash.startsWith('scenario')) return { screen: 'scenario' };
  if (hash.startsWith('check')) {
    const runId = hash.split('/')[1];
    return { screen: 'check', runId };
  }
  if (hash.startsWith('monitor')) {
    const runId = hash.split('/')[1];
    return { screen: 'monitor', runId };
  }
  if (hash.startsWith('evaluator')) {
    const runId = hash.split('/')[1];
    return { screen: 'evaluator', runId };
  }
  // Legacy route aliases — redirect to canonical names
  if (hash.startsWith('builder')) return { screen: 'scenario' };
  if (hash.startsWith('preflight')) {
    const runId = hash.split('/')[1];
    return { screen: 'check', runId };
  }
  if (hash.startsWith('bridge')) {
    const runId = hash.split('/')[1];
    return { screen: 'monitor', runId };
  }
  if (hash.startsWith('report')) {
    const runId = hash.split('/')[1];
    return { screen: 'evaluator', runId };
  }
  return { screen: 'scenario' };
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
        {route.screen === 'scenario'   && <SimulationScenario />}
        {route.screen === 'check'      && <SimulationCheck />}
        {route.screen === 'monitor'    && <SimulationMonitor />}
        {route.screen === 'evaluator'  && <SimulationEvaluator />}
      </div>
      <FooterHotkeyHints screen={route.screen} />
    </div>
  );
}
