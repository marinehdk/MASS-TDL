import { useState, useEffect } from 'react';
import { ScenarioBuilder } from './screens/ScenarioBuilder';
import { Preflight } from './screens/Preflight';
import { BridgeHMI } from './screens/BridgeHMI';
import { RunReport } from './screens/RunReport';

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

  switch (route.screen) {
    case 'preflight':
      return <Preflight />;
    case 'bridge':
      return <BridgeHMI />;
    case 'report':
      return <RunReport />;
    default:
      return <ScenarioBuilder />;
  }
}
