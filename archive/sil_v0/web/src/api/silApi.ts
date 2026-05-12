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
  module_id: string;
  state: string;
  latency_ms: number;
  message_drops: number;
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

    activateLifecycle: builder.mutation<{ success: boolean; error?: string }, void>({
      query: () => ({ url: '/lifecycle/activate', method: 'POST' }),
    }),

    deactivateLifecycle: builder.mutation<{ success: boolean; error?: string }, void>({
      query: () => ({ url: '/lifecycle/deactivate', method: 'POST' }),
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
  useProbeSelfCheckMutation,
  useGetHealthStatusQuery,
  useExportMarzipMutation,
  useGetExportStatusQuery,
} = silApi;
