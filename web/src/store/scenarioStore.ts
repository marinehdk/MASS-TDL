import { create } from 'zustand';
import type { LifecycleStatus } from '../types';

type LifecycleStateValue = LifecycleStatus['currentState'];

interface ScenarioState {
  scenarioId: string | null;
  runId: string | null;
  scenarioHash: string | null;
  lifecycleState: LifecycleStateValue | null;

  setScenario: (id: string, hash: string) => void;
  setRunId: (runId: string) => void;
  setLifecycleState: (state: LifecycleStateValue) => void;
  reset: () => void;
}

export const useScenarioStore = create<ScenarioState>((set) => ({
  scenarioId: null,
  runId: null,
  scenarioHash: null,
  lifecycleState: null,
  setScenario: (scenarioId, scenarioHash) => set({ scenarioId, scenarioHash }),
  setRunId: (runId) => set({ runId }),
  setLifecycleState: (lifecycleState) => set({ lifecycleState }),
  reset: () => set({ scenarioId: null, runId: null, scenarioHash: null, lifecycleState: null }),
}));
