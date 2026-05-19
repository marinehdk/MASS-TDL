import { create } from 'zustand';
import type { LifecycleStatus } from '../types';

type LifecycleStateValue = LifecycleStatus['currentState'];

interface ScenarioState {
  scenarioId: string | null;
  runId: string | null;
  scenarioHash: string | null;
  lifecycleState: LifecycleStateValue | null;
  yamlValid: boolean;
  yamlError: string | null;

  setScenario: (id: string, hash: string) => void;
  setRunId: (runId: string) => void;
  setLifecycleState: (state: LifecycleStateValue) => void;
  setYamlValidation: (valid: boolean, error: string | null) => void;
  reset: () => void;
}

export const useScenarioStore = create<ScenarioState>((set) => ({
  scenarioId: null,
  runId: null,
  scenarioHash: null,
  lifecycleState: null,
  yamlValid: true,
  yamlError: null,
  setScenario: (scenarioId, scenarioHash) => set({ scenarioId, scenarioHash }),
  setRunId: (runId) => set({ runId }),
  setLifecycleState: (lifecycleState) => set({ lifecycleState }),
  setYamlValidation: (yamlValid, yamlError) => set({ yamlValid, yamlError }),
  reset: () => set({ scenarioId: null, runId: null, scenarioHash: null, lifecycleState: null, yamlValid: true, yamlError: null }),
}));
