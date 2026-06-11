import { createApi, fetchBaseQuery } from '@reduxjs/toolkit/query/react';
import type { LifecycleStatus } from '../types';
import type { OpsResult } from '../types/gateStream';

export interface ScenarioSummary {
  id: string;
  name: string;
  encounter_type: string;
  folder: string;
  is_baseline: boolean;
  folder_tags?: string[];
  last_ci_result?: string | null;
  latitude?: number;
  longitude?: number;
  odd_domain?: string;
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

export interface AsdrEvent {
  t: number;
  k: string;
  sev: string;
  m: string;
  d: string;
}

export interface AsdrLedgerEntry {
  time: string;
  type: string;
  module: string;
  payload: string;
  hash: string;
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

export interface ScoringLastRunFull {
  run_id: string | null;
  scenario_id?: string;
  kpis: {
    // Legacy fields (demo backend)
    min_cpa_nm: number;
    avg_rot_dpm: number;
    distance_nm: number;
    duration_s: number;
    // New fields (Arrow path)
    tcpa_min_s: number;
    max_rudder_deg: number;
    grounding_risk_score: number;
    route_deviation_nm: number;
    time_to_mrm_s: number;
    decision_count: number;
  } | null;
  scoring_dimensions: {
    safety: number;
    rule_compliance: number;
    delay_penalty: number;
    action_magnitude_penalty: number;
    phase_score: number;
    plausibility: number;
    total: number;
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

    updateScenario: builder.mutation<{ hash: string }, { id: string; yaml_content: string }>({
      query: ({ id, yaml_content }) => ({
        url: `/scenarios/${id}`,
        method: 'PUT',
        body: { yaml_content },
      }),
      invalidatesTags: (_result, _error, { id }) => [{ type: 'Scenario', id }],
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

    changeLifecycleRate: builder.mutation<{ success: boolean; error?: string }, number>({
      query: (rate) => ({
        url: '/lifecycle/rate',
        method: 'POST',
        body: { rate },
      }),
    }),

    // Scoring (Screen ④)
    getLastRunScoring: builder.query<ScoringLastRunFull, void>({
      query: () => '/scoring/last_run',
    }),

    getAsdrEvents: builder.query<{
      events: AsdrEvent[];
      ledger: AsdrLedgerEntry[];
    }, void>({
      query: () => '/asdr/events',
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
    injectEncounter: builder.mutation<
      { accepted: boolean; mmsi: number },
      { rule: string; range_nm?: number; construct_cpa_m?: number; approach_angle_deg?: number }
    >({
      query: (body) => ({ url: '/encounters/inject', method: 'POST', body }),
    }),
    removeEncounter: builder.mutation<{ removed: boolean }, number>({
      query: (mmsi) => ({ url: `/encounters/${mmsi}`, method: 'DELETE' }),
    }),
    clearEncounters: builder.mutation<{ removed_count: number; failed_mmsis?: number[]; stale_mmsis?: number[] }, void>({
      query: () => ({ url: '/encounters', method: 'DELETE' }),
    }),
    skipPreflight: builder.mutation<{ skipped: boolean; verdict: string }, { scenario_id: string; reason: string }>({
      query: (body) => ({
        url: `/selfcheck/skip`,
        method: 'POST',
        params: { scenario_id: body.scenario_id, reason: body.reason },
      }),
    }),

    // --- Ops Quick Fix mutations ---
    restartNode: builder.mutation<OpsResult, string>({
      query: (name) => ({ url: `/api/v1/ops/restart_node?name=${encodeURIComponent(name)}`, method: 'POST' }),
    }),
    restartServices: builder.mutation<OpsResult, void>({
      query: () => ({ url: '/api/v1/ops/restart_services', method: 'POST' }),
    }),
    syncTime: builder.mutation<OpsResult, void>({
      query: () => ({ url: '/api/v1/ops/sync_time', method: 'POST' }),
    }),
    clearHashCache: builder.mutation<OpsResult, string>({
      query: (scenarioId) => ({ url: `/api/v1/ops/clear_hash_cache?scenario_id=${encodeURIComponent(scenarioId)}`, method: 'POST' }),
    }),
    ensureAsdrDir: builder.mutation<OpsResult, string>({
      query: (runId) => ({ url: `/api/v1/ops/ensure_asdr_dir?run_id=${encodeURIComponent(runId)}`, method: 'POST' }),
    }),
  }),
});

export const {
  useListScenariosQuery,
  useGetScenarioQuery,
  useValidateScenarioMutation,
  useCreateScenarioMutation,
  useUpdateScenarioMutation,
  useDeleteScenarioMutation,
  useGetLifecycleStatusQuery,
  useConfigureLifecycleMutation,
  useActivateLifecycleMutation,
  useDeactivateLifecycleMutation,
  useCleanupLifecycleMutation,
  useChangeLifecycleRateMutation,
  useGetLastRunScoringQuery,
  useGetAsdrEventsQuery,
  useProbeSelfCheckMutation,
  useGetHealthStatusQuery,
  useExportMarzipMutation,
  useGetExportStatusQuery,
  useTriggerFaultMutation,
  useInjectFaultMutation,
  useCancelFaultMutation,
  useSkipPreflightMutation,
  useRestartNodeMutation,
  useRestartServicesMutation,
  useSyncTimeMutation,
  useClearHashCacheMutation,
  useEnsureAsdrDirMutation,
  useInjectEncounterMutation,
  useRemoveEncounterMutation,
  useClearEncountersMutation,
} = silApi;
