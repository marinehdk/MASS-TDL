import { createApi, fetchBaseQuery } from '@reduxjs/toolkit/query/react';
import type { LifecycleStatus } from '../types';

export interface ScenarioSummary {
  id: string;
  name: string;
  encounter_type: string;
}

export interface ScenarioDetail {
  yaml_content: string;
  hash: string;
}

export interface ValidateResult {
  valid: boolean;
  errors: string[];
}

export interface ProbeResult {
  all_clear: boolean;
  items: { name: string; passed: boolean; detail: string }[];
}

export interface ModulePulseStatus {
  moduleId: string;
  state: number;       // 1=GREEN, 2=AMBER, 3=RED (matches Protobuf enum)
  latencyMs: number;
  messageDrops: number;
}

export interface ScoringLastRun {
  run_id: string | null;
  scenario_id?: string;
  kpis: {
    min_cpa_nm: number;
    avg_rot_dpm: number;
    distance_nm: number;
    duration_s: number;
  } | null;
  rule_chain: string[];
  verdict?: 'pass' | 'fail' | 'pending';
}

export const silApi = createApi({
  reducerPath: 'silApi',
  baseQuery: fetchBaseQuery({ baseUrl: '/api/v1' }),
  tagTypes: ['Scenario', 'Run'],
  endpoints: (builder) => ({

    // Scenario CRUD
    listScenarios: builder.query<ScenarioSummary[], void>({
      query: () => '/scenarios',
      providesTags: ['Scenario'],
    }),

    getScenario: builder.query<ScenarioDetail, string>({
      query: (id) => `/scenarios/${id}`,
      providesTags: (_result, _error, id) => [{ type: 'Scenario', id }],
    }),

    validateScenario: builder.mutation<ValidateResult, string>({
      query: (yamlContent) => ({
        url: '/scenarios/validate',
        method: 'POST',
        body: { yaml_content: yamlContent },
      }),
    }),

    createScenario: builder.mutation<{ scenario_id: string; hash: string }, string>({
      query: (yamlContent) => ({
        url: '/scenarios',
        method: 'POST',
        body: { yaml_content: yamlContent },
      }),
      invalidatesTags: ['Scenario'],
    }),

    deleteScenario: builder.mutation<void, string>({
      query: (id) => ({ url: `/scenarios/${id}`, method: 'DELETE' }),
      invalidatesTags: ['Scenario'],
    }),

    // Lifecycle
    getLifecycleStatus: builder.query<LifecycleStatus, void>({
      query: () => '/lifecycle/status',
    }),

    configureLifecycle: builder.mutation<{ success: boolean; error?: string }, string>({
      query: (scenarioId) => ({
        url: '/lifecycle/configure',
        method: 'POST',
        body: { scenario_id: scenarioId },
      }),
    }),

    activateLifecycle: builder.mutation<{ success: boolean; error?: string; run_id?: string }, void>({
      query: () => ({ url: '/lifecycle/activate', method: 'POST' }),
    }),

    deactivateLifecycle: builder.mutation<{ success: boolean; error?: string; run_id?: string }, void>({
      query: () => ({ url: '/lifecycle/deactivate', method: 'POST' }),
    }),

    cleanupLifecycle: builder.mutation<{ success: boolean; error?: string }, void>({
      query: () => ({ url: '/lifecycle/cleanup', method: 'POST' }),
    }),

    // Scoring (Screen ④)
    getLastRunScoring: builder.query<ScoringLastRun, void>({
      query: () => '/scoring/last_run',
    }),

    // Self-check
    probeSelfCheck: builder.mutation<ProbeResult, void>({
      query: () => ({ url: '/selfcheck/probe', method: 'POST' }),
    }),

    getHealthStatus: builder.query<{ module_pulses: ModulePulseStatus[] }, void>({
      query: () => '/selfcheck/status',
    }),

    // Export
    exportMarzip: builder.mutation<{ download_url: string; status: string }, string>({
      query: (runId) => ({
        url: '/export/marzip',
        method: 'POST',
        body: { run_id: runId },
      }),
    }),

    getExportStatus: builder.query<{ status: string; download_url?: string }, string>({
      query: (runId) => `/export/status/${runId}`,
    }),
  }),
});

export const {
  useListScenariosQuery,
  useGetScenarioQuery,
  useValidateScenarioMutation,
  useCreateScenarioMutation,
  useDeleteScenarioMutation,
  useGetLifecycleStatusQuery,
  useConfigureLifecycleMutation,
  useActivateLifecycleMutation,
  useDeactivateLifecycleMutation,
  useCleanupLifecycleMutation,
  useGetLastRunScoringQuery,
  useProbeSelfCheckMutation,
  useGetHealthStatusQuery,
  useExportMarzipMutation,
  useGetExportStatusQuery,
} = silApi;
