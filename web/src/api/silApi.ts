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

export type IntegrationProfileMode = 'default' | 'external' | 'hybrid_debug';
export type IntegrationAdapterState = 'enabled' | 'disabled';

export interface IntegrationProfileSummary {
  name: string;
  mode?: IntegrationProfileMode;
  tdl_domain_id?: number;
  external_enabled?: boolean;
  adapters?: Record<string, IntegrationAdapterState>;
  external_domains?: Record<string, {
    domain_id: number;
    workspace_setup?: string | null;
    required_topics?: Record<string, string>;
  }>;
}

export type IntegrationProfileEntry = string | IntegrationProfileSummary;

export interface IntegrationProfilesResult {
  active_profile: string;
  profiles: IntegrationProfileEntry[];
}

export interface IntegrationStatus {
  active_profile: string;
  external_enabled: boolean;
  route_out_enabled: boolean;
}

export interface IntegrationProbeCheck {
  gate_id: number;
  label: string;
  passed: boolean;
  detail: string;
}

export interface IntegrationProbeResult {
  profile_name: string;
  all_clear: boolean;
  checks: IntegrationProbeCheck[];
}

export interface IntegrationProfileDetail extends IntegrationProfileSummary {
  name: string;
  mode: IntegrationProfileMode;
}

export type RuntimeMode = 'internal' | 'integration';
export type RuntimeTarget = 'local' | 'a4000';
export type RuntimeVerdict = 'GO' | 'NO-GO' | 'CHECKING' | 'IDLE';
export type RuntimeServiceStatus = 'running' | 'stopped' | 'unknown';
export type RuntimeHealthStatus = 'healthy' | 'starting' | 'degraded' | 'unhealthy' | 'unknown';
export type RuntimePluginRoleName = 'hydrodynamics' | 'route_l2' | 'fusion';
export type RuntimeTopicStatus = 'ok' | 'missing' | 'wrong_type' | 'stale' | 'unchecked';

export interface RuntimeCoreService {
  id: string;
  service: string;
  class: 'core_service';
  container_name: string;
  status: RuntimeServiceStatus;
  health: RuntimeHealthStatus;
  image: string;
  allowed_actions: string[];
}

export interface RuntimeCoreServicesGate {
  name: 'core_services_running';
  passed: boolean;
  services: Record<string, RuntimeServiceStatus>;
}

export interface RuntimePluginRoleGate {
  role: RuntimePluginRoleName;
  active_plugin: string | null;
  running_plugins: string[];
  passed: boolean;
}

export interface RuntimeSingleActivePluginGate {
  name: 'single_active_plugin_per_role';
  passed: boolean;
  roles: RuntimePluginRoleGate[];
}

export type RuntimeGate = RuntimeCoreServicesGate | RuntimeSingleActivePluginGate;

export interface RuntimePlugin {
  id: string;
  label: string;
  service: string;
  container: string;
  status: RuntimeServiceStatus;
  health: RuntimeHealthStatus;
  image: string;
  expected_image: string;
  revision: string;
  revision_label: string;
  required_topics: Record<string, string>;
  topic_status: RuntimeTopicStatus;
  health_required: boolean;
  ros_domain_id: number;
}

export interface RuntimePluginRole {
  role: RuntimePluginRoleName;
  active_plugin: string | null;
  single_instance: boolean;
  plugins: RuntimePlugin[];
}

export interface RuntimeSummary {
  mode: RuntimeMode;
  target: RuntimeTarget;
  active_profile: string;
  verdict: RuntimeVerdict;
  core_services: RuntimeCoreService[];
  plugin_roles: RuntimePluginRole[];
  gates: RuntimeGate[];
  evidence_path?: string;
}

export interface RuntimeActionResult {
  accepted: boolean;
  action?: string;
  service?: string;
  role?: string;
  old_plugin?: string | null;
  new_plugin?: string;
  stopped_service?: string | null;
  started_service?: string;
  error?: string;
}

export interface RuntimeCoreServicesResult {
  services: RuntimeCoreService[];
}

export interface RuntimePluginsResult {
  roles: RuntimePluginRole[];
}

export interface EvidenceSessionStartRequest {
  source: 'frontend' | 'cli';
  suite: 'frontend' | 'single' | 'clean8' | 'clean12';
  scenario_id?: string;
}

export interface EvidenceSessionStartResponse {
  session_id: string;
  session_name: string;
  path: string;
  manifest: Record<string, unknown>;
}

export interface EvidenceSessionFinalizeRequest {
  sessionId: string;
  scenario_id: string;
  status: 'completed' | 'stopped' | 'error';
  run_id?: string;
}

export interface EvidenceLibrarySession {
  evidence_id: string;
  session_id: string;
  source: string;
  suite: string;
  root_id: string;
  worktree_name?: string | null;
  branch?: string | null;
  session_path: string;
  deletion_allowed: boolean;
  deletion_target: string | null;
  deletion_error?: string | null;
  created_at?: string | null;
  ended_at?: string | null;
  status?: string | null;
  valid_data: number | boolean;
  scenario_count: number;
  passed_scenarios?: number;
  failed_scenarios?: number;
  scenario_ids: string[];
  overview_png?: { scenario_id: string; relative_path: string } | null;
  overview_pngs?: { scenario_id: string; relative_path: string }[];
  ingest_status: string;
  ingest_error?: string | null;
}

export type EvidenceLibrarySortKey = 'time' | 'result' | 'scenarioCount' | 'mode' | 'scenario' | 'source' | 'worktree';
export type EvidenceLibraryOutcome = 'passed' | 'failed' | 'unknown';

export interface EvidenceLibraryFacetOption {
  value: string;
  label: string;
  count: number;
}

export interface EvidenceLibrarySessionsQuery {
  page: number;
  page_size: 20 | 50;
  search?: string;
  sort_key: EvidenceLibrarySortKey;
  sort_direction: 'asc' | 'desc';
  result?: EvidenceLibraryOutcome;
  scenario_count?: number;
  mode?: string;
  scenario?: string;
  source?: string;
  worktree?: string;
}

export interface EvidenceLibrarySessionsResponse {
  sessions: EvidenceLibrarySession[];
  total: number;
  filtered_total: number;
  page: number;
  page_size: 20 | 50;
  total_pages: number;
  facets: Record<
    'result' | 'scenarioCount' | 'mode' | 'scenario' | 'source' | 'worktree',
    EvidenceLibraryFacetOption[]
  >;
}

export interface EvidenceLibraryScanResult {
  job_id: string | null;
  state: 'idle' | 'queued' | 'running' | 'completed' | 'failed';
  force: boolean;
  total: number;
  processed: number;
  ingested: number;
  skipped: number;
  pruned: number;
  errors: Array<{ path: string; error: string }>;
  cleanup_pending: EvidenceLibraryDeleteResult[];
  started_at: string | null;
  finished_at: string | null;
}

export interface EvidenceLibraryDeleteResult {
  evidence_id: string;
  deleted_path: string;
  filesystem_deleted: boolean;
  filesystem_cleanup: 'completed' | 'not_needed' | 'pending';
  cleanup_error?: string;
  cleanup_path?: string;
  cleanup_metadata_path?: string;
  cleanup_paths?: string[];
}

export interface EvidenceLibraryBatchDeleteRequest {
  evidence_ids: string[];
}

export type EvidenceLibraryBatchDeleteItem =
  | (EvidenceLibraryDeleteResult & { status: 'deleted' })
  | { evidence_id: string; status: 'failed'; error: string };

export interface EvidenceLibraryBatchDeleteResult {
  requested: number;
  deleted: number;
  failed: number;
  results: EvidenceLibraryBatchDeleteItem[];
}

export interface EvidenceReplayTrajectoryPoint {
  vessel_id: string;
  vessel_role: string;
  sim_t: number;
  wall_t?: number | null;
  lat?: number | null;
  lon?: number | null;
  heading_deg?: number | null;
  sog_kn?: number | null;
  rot_deg_s?: number | null;
  source_topic?: string | null;
  sample_seq?: number;
}

export interface EvidenceReplayEvent {
  event_id?: number;
  sim_t: number;
  wall_t?: number | null;
  module: string;
  event_type: string;
  severity: string;
  payload_json: string;
  source_topic?: string | null;
}

export interface EvidenceGateResult {
  gate_id: string;
  status: string;
  temporal_scope: string;
  payload_json: string;
  source: string;
}

export type EvidenceReplaySession = Omit<
  EvidenceLibrarySession,
  'scenario_ids' | 'deletion_allowed' | 'deletion_target' | 'deletion_error'
> & {
  scenario_ids?: string[];
};

export interface EvidenceReplayResponse {
  session: EvidenceReplaySession;
  scenario: {
    scenario_id: string;
    verdict?: string | null;
    overall_pass?: boolean | number | null;
    min_cpa_nm?: number | null;
  };
  duration_s: number;
  trajectory: EvidenceReplayTrajectoryPoint[];
  events: EvidenceReplayEvent[];
  gates: EvidenceGateResult[];
  artifacts: Array<{ artifact_id: number; kind: string; relative_path: string; available: number | boolean }>;
}

export interface EvidenceDecisionFrame {
  evidence_id: string;
  scenario_id: string;
  sim_t: number;
  chain: Record<string, { status: string; status_source: string; facts: Record<string, unknown> }>;
  gates: EvidenceGateResult[];
  nearby_events: EvidenceReplayEvent[];
}

export const silApi = createApi({
  reducerPath: 'silApi',
  baseQuery: fetchBaseQuery({ baseUrl: '/api/v1' }),
  tagTypes: ['Scenario', 'Run', 'Integration', 'Runtime', 'EvidenceLibrary'],
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

    // Evidence sessions
    startEvidenceSession: builder.mutation<EvidenceSessionStartResponse, EvidenceSessionStartRequest>({
      query: (body) => ({
        url: '/evidence/session/start',
        method: 'POST',
        body,
      }),
    }),

    finalizeEvidenceSession: builder.mutation<Record<string, unknown>, EvidenceSessionFinalizeRequest>({
      query: ({ sessionId, ...body }) => ({
        url: `/evidence/session/${encodeURIComponent(sessionId)}/finalize`,
        method: 'POST',
        body,
      }),
    }),

    getEvidenceLibrarySessions: builder.query<EvidenceLibrarySessionsResponse, EvidenceLibrarySessionsQuery>({
      query: (params) => ({ url: '/evidence-library/sessions', params }),
      providesTags: ['EvidenceLibrary'],
    }),

    rescanEvidenceLibrary: builder.mutation<EvidenceLibraryScanResult, { force?: boolean }>({
      query: (body) => ({
        url: '/evidence-library/rescan',
        method: 'POST',
        body,
      }),
      invalidatesTags: (result) => (
        result && result.state !== 'queued' && result.state !== 'running'
          ? ['EvidenceLibrary']
          : []
      ),
    }),

    getEvidenceLibraryRescanStatus: builder.query<EvidenceLibraryScanResult, void>({
      query: () => '/evidence-library/rescan/status',
    }),

    deleteEvidenceLibrarySession: builder.mutation<EvidenceLibraryDeleteResult, string>({
      query: (evidenceId) => ({
        url: `/evidence-library/sessions/${encodeURIComponent(evidenceId)}`,
        method: 'DELETE',
      }),
      async onQueryStarted(evidenceId, { dispatch, getState, queryFulfilled }) {
        try {
          await queryFulfilled;
        } catch {
          // Rejected deletes leave the indexed-session cache unchanged.
          return;
        }

        const cachedListQueries = silApi.util.selectInvalidatedBy(getState(), ['EvidenceLibrary'])
          .filter((query) => query.endpointName === 'getEvidenceLibrarySessions');
        for (const query of cachedListQueries) {
          const runningListQuery = dispatch(
            silApi.util.getRunningQueryThunk(
              'getEvidenceLibrarySessions',
              query.originalArgs as EvidenceLibrarySessionsQuery,
            ),
          );
          if (runningListQuery) {
            runningListQuery.abort();
            await runningListQuery;
          }
        }

        for (const query of cachedListQueries) {
          dispatch(silApi.util.updateQueryData(
            'getEvidenceLibrarySessions',
            query.originalArgs as EvidenceLibrarySessionsQuery,
            (draft) => {
              draft.sessions = draft.sessions.filter((session) => session.evidence_id !== evidenceId);
            },
          ));
        }
        dispatch(silApi.util.invalidateTags(['EvidenceLibrary']));
        for (const query of cachedListQueries) {
          const refresh = dispatch(
            silApi.util.getRunningQueryThunk(
              'getEvidenceLibrarySessions',
              query.originalArgs as EvidenceLibrarySessionsQuery,
            ),
          );
          if (refresh) await refresh;
        }
      },
    }),

    batchDeleteEvidenceLibrarySessions: builder.mutation<
      EvidenceLibraryBatchDeleteResult,
      EvidenceLibraryBatchDeleteRequest
    >({
      query: (body) => ({
        url: '/evidence-library/sessions/batch-delete',
        method: 'POST',
        body,
      }),
      async onQueryStarted(_request, { dispatch, getState, queryFulfilled }) {
        let result: EvidenceLibraryBatchDeleteResult;
        try {
          ({ data: result } = await queryFulfilled);
        } catch {
          // Response loss can hide a committed delete; reconcile from the authoritative list.
          dispatch(silApi.util.invalidateTags(['EvidenceLibrary']));
          return;
        }

        const deletedIds = new Set(
          result.results
            .filter((item) => item.status === 'deleted')
            .map((item) => item.evidence_id),
        );
        const cachedListQueries = silApi.util.selectInvalidatedBy(getState(), ['EvidenceLibrary'])
          .filter((query) => query.endpointName === 'getEvidenceLibrarySessions');
        for (const query of cachedListQueries) {
          const runningListQuery = dispatch(
            silApi.util.getRunningQueryThunk(
              'getEvidenceLibrarySessions',
              query.originalArgs as EvidenceLibrarySessionsQuery,
            ),
          );
          if (runningListQuery) {
            runningListQuery.abort();
            await runningListQuery;
          }
        }

        for (const query of cachedListQueries) {
          dispatch(silApi.util.updateQueryData(
            'getEvidenceLibrarySessions',
            query.originalArgs as EvidenceLibrarySessionsQuery,
            (draft) => {
              draft.sessions = draft.sessions.filter((session) => !deletedIds.has(session.evidence_id));
            },
          ));
        }
        dispatch(silApi.util.invalidateTags(['EvidenceLibrary']));
        for (const query of cachedListQueries) {
          const refresh = dispatch(
            silApi.util.getRunningQueryThunk(
              'getEvidenceLibrarySessions',
              query.originalArgs as EvidenceLibrarySessionsQuery,
            ),
          );
          if (refresh) await refresh;
        }
      },
    }),

    getEvidenceReplay: builder.query<EvidenceReplayResponse, { evidenceId: string; scenarioId: string }>({
      query: ({ evidenceId, scenarioId }) =>
        `/evidence-library/sessions/${encodeURIComponent(evidenceId)}/scenarios/${encodeURIComponent(scenarioId)}/replay`,
    }),

    getDecisionFrame: builder.query<EvidenceDecisionFrame, { evidenceId: string; scenarioId: string; simT: number }>({
      query: ({ evidenceId, scenarioId, simT }) =>
        `/evidence-library/sessions/${encodeURIComponent(evidenceId)}/scenarios/${encodeURIComponent(scenarioId)}/decision-frame?sim_t=${encodeURIComponent(simT)}`,
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

    // External integration
    listIntegrationProfiles: builder.query<IntegrationProfilesResult, void>({
      query: () => '/integration/profiles',
      providesTags: ['Integration'],
    }),

    getIntegrationStatus: builder.query<IntegrationStatus, void>({
      query: () => '/integration/status',
      providesTags: ['Integration'],
    }),

    selectIntegrationProfile: builder.mutation<IntegrationProfileDetail, { name: string }>({
      query: (body) => ({
        url: '/integration/profile',
        method: 'POST',
        body,
      }),
      invalidatesTags: ['Integration'],
    }),

    probeIntegration: builder.mutation<IntegrationProbeResult, void>({
      query: () => ({ url: '/integration/probe', method: 'POST' }),
    }),

    getRuntimeSummary: builder.query<RuntimeSummary, void>({
      query: () => '/runtime/summary',
      providesTags: ['Runtime'],
    }),

    getRuntimeCoreServices: builder.query<RuntimeCoreServicesResult, void>({
      query: () => '/runtime/core-services',
      providesTags: ['Runtime'],
    }),

    getRuntimePlugins: builder.query<RuntimePluginsResult, void>({
      query: () => '/runtime/plugins',
      providesTags: ['Runtime'],
    }),

    restartRuntimeCoreService: builder.mutation<RuntimeActionResult, string>({
      query: (serviceId) => ({
        url: `/runtime/core/${encodeURIComponent(serviceId)}/restart`,
        method: 'POST',
      }),
      invalidatesTags: ['Runtime'],
    }),

    startRuntimeCoreStack: builder.mutation<RuntimeActionResult, void>({
      query: () => ({ url: '/runtime/core/start', method: 'POST' }),
      invalidatesTags: ['Runtime'],
    }),

    restartRuntimeCoreStack: builder.mutation<RuntimeActionResult, void>({
      query: () => ({ url: '/runtime/core/restart', method: 'POST' }),
      invalidatesTags: ['Runtime'],
    }),

    stopRuntimeCoreStack: builder.mutation<RuntimeActionResult, { confirm: string }>({
      query: (body) => ({ url: '/runtime/core/stop', method: 'POST', body }),
      invalidatesTags: ['Runtime'],
    }),

    switchRuntimePlugin: builder.mutation<RuntimeActionResult, { role: string; plugin_id: string }>({
      query: ({ role, plugin_id }) => ({
        url: `/runtime/plugins/${encodeURIComponent(role)}/switch`,
        method: 'POST',
        body: { plugin_id },
      }),
      invalidatesTags: ['Runtime'],
    }),

    probeRuntime: builder.mutation<RuntimeSummary, void>({
      query: () => ({ url: '/runtime/probe', method: 'POST' }),
      invalidatesTags: ['Runtime'],
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
  useStartEvidenceSessionMutation,
  useFinalizeEvidenceSessionMutation,
  useGetEvidenceLibrarySessionsQuery,
  useRescanEvidenceLibraryMutation,
  useLazyGetEvidenceLibraryRescanStatusQuery,
  useDeleteEvidenceLibrarySessionMutation,
  useBatchDeleteEvidenceLibrarySessionsMutation,
  useGetEvidenceReplayQuery,
  useGetDecisionFrameQuery,
  useGetLastRunScoringQuery,
  useGetAsdrEventsQuery,
  useProbeSelfCheckMutation,
  useGetHealthStatusQuery,
  useListIntegrationProfilesQuery,
  useGetIntegrationStatusQuery,
  useLazyGetIntegrationStatusQuery,
  useSelectIntegrationProfileMutation,
  useProbeIntegrationMutation,
  useGetRuntimeSummaryQuery,
  useGetRuntimeCoreServicesQuery,
  useGetRuntimePluginsQuery,
  useRestartRuntimeCoreServiceMutation,
  useStartRuntimeCoreStackMutation,
  useRestartRuntimeCoreStackMutation,
  useStopRuntimeCoreStackMutation,
  useSwitchRuntimePluginMutation,
  useProbeRuntimeMutation,
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
