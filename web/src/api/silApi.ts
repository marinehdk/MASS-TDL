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

export interface GateCheckResult {
  gate_id: number;
  label: string;
  passed: boolean;
  checks: string[];
  duration_ms: number;
  rationale: string;
}

export interface ProbeResult {
  all_clear: boolean;
  go_no_go: 'GO' | 'NO-GO';
  scenario_id: string;
  gates: GateCheckResult[];
  items?: { name: string; passed: boolean; detail: string }[];
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
    probeSelfCheck: builder.mutation<ProbeResult, { scenario_id?: string } | void>({
      query: (arg) => {
        const params = (arg && 'scenario_id' in arg && arg.scenario_id)
          ? `?scenario_id=${encodeURIComponent(arg.scenario_id)}`
          : '';
        return { url: `/selfcheck/probe${params}`, method: 'POST' };
      },
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
    // Fault injection
    triggerFault: builder.mutation<{ fault_id?: string }, { fault_type: string; payload_json: string }>({
      query: (body) => ({ url: '/fault/trigger', method: 'POST', body }),
    }),

    // Fault injection (v1.1 NEW — Task 20)
    injectFault: builder.mutation<{ accepted: boolean; fault_id: string }, { type: string; duration_s: number; params?: any }>({
      query: (body) => ({ url: '/fault/inject', method: 'POST', body }),
    }),
    cancelFault: builder.mutation<{ cancelled: boolean }, string>({
      query: (faultId) => ({ url: `/fault/${faultId}`, method: 'DELETE' }),
    }),
    skipPreflight: builder.mutation<{ skipped: boolean; verdict: string }, { scenario_id: string; reason: string }>({
      query: (body) => ({
        url: `/selfcheck/skip`,
        method: 'POST',
        params: { scenario_id: body.scenario_id, reason: body.reason },
      }),
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
  useTriggerFaultMutation,
  useInjectFaultMutation,
  useCancelFaultMutation,
  useSkipPreflightMutation,
} = silApi;
