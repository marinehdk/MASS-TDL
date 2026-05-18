// === SSE Gate 事件 ===
export interface GateCheckItem {
  item: string;
  status: 'ok' | 'fail' | 'warn';
  detail: string;
}

export interface GateSSEEvent {
  gate_id: number;
  label: string;
  passed: boolean;
  checks: GateCheckItem[];
  duration_ms: number;
  rationale: string;
}

export interface SSECompleteEvent {
  type: 'complete';
  all_clear: boolean;
  go_no_go: 'GO' | 'NO-GO';
}

// === Ops 端点类型 ===
export interface OpsResult {
  success: boolean;
  message: string;
  duration_ms: number;
}

export interface OpsRestartNodeRequest {
  name: string;
}

export interface OpsClearHashCacheRequest {
  scenario_id: string;
}

export interface OpsEnsureAsdrDirRequest {
  run_id: string;
}

// === useGateStream hook 返回类型 ===
export interface GateStreamState {
  gates: GateSSEEvent[];
  verdict: 'GO' | 'NO-GO' | null;
  streaming: boolean;
}
