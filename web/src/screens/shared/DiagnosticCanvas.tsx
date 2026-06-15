import type { GateSSEEvent } from '../../types/gateStream';
import { Ros2TopologySvg } from './Ros2TopologySvg';
import { YamlDiffViewer } from './YamlDiffViewer';
import { ContainerBoundarySvg } from './ContainerBoundarySvg';

interface DiagnosticCanvasProps {
  focusedGateId: number | null;
  gates: GateSSEEvent[];
  scenarioYaml: string;
  storedYaml: string;
  verdict: 'GO' | 'NO-GO' | null;
  countdown: number;
  transitioning: boolean;
}

export function DiagnosticCanvas({ focusedGateId, gates, scenarioYaml, storedYaml, verdict, countdown: _countdown, transitioning }: DiagnosticCanvasProps) {
  if (transitioning) {
    return (
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', background: 'rgba(0,227,179,0.08)' }}>
        <div style={{ width: 48, height: 48, borderRadius: '50%', border: '3px solid var(--c-stbd)', borderTopColor: 'transparent', animation: 'spin 1s linear infinite', marginBottom: 20 }} />
        <div style={{ fontFamily: 'var(--f-disp)', fontSize: 20, color: 'var(--c-stbd)', marginBottom: 8 }}>
          正在激活 L3 核心...
        </div>
        <div style={{ fontFamily: 'var(--f-body)', fontSize: 13, color: 'var(--txt-2)' }}>
          ROS2 生命周期节点启动中
        </div>
      </div>
    );
  }
  if (verdict === 'GO') {
    return (
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', background: 'rgba(0,227,179,0.08)' }}>
        <div style={{ fontFamily: 'var(--f-disp)', fontSize: 22, color: 'var(--c-stbd)', marginBottom: 12 }}>
          所有安全门控通过
        </div>
        <div style={{ fontFamily: 'var(--f-body)', fontSize: 14, color: 'var(--txt-2)' }}>
          请在右下角人工确认 GO 后进入仿真运行
        </div>
      </div>
    );
  }

  if (!focusedGateId || gates.length === 0) {
    return (
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', background: 'var(--bg-0)' }}>
        <div style={{ width: 60, height: 60, borderRadius: '50%', border: '3px solid var(--line-2)', borderTopColor: 'var(--c-phos)', animation: 'spin 1s linear infinite' }} />
        <div style={{ marginTop: 16, fontFamily: 'var(--f-disp)', fontSize: 14, color: 'var(--txt-1)' }}>
          {gates.length > 0 ? `Checking Gate ${gates.length + 1}...` : 'Initializing...'}
        </div>
        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-2)', marginTop: 4 }}>
          {gates.length > 0 ? `${gates.length}/6 complete` : 'Waiting for SSE stream'}
        </div>
      </div>
    );
  }

  const gate = gates.find(g => g.gate_id === focusedGateId);
  if (!gate) return null;

  if (focusedGateId === 1 || focusedGateId === 2) {
    return <Ros2TopologySvg gates={gates} focusedGateId={focusedGateId} />;
  }
  if (focusedGateId === 3 || focusedGateId === 4) {
    return <YamlDiffViewer original={storedYaml} modified={scenarioYaml} gate={gate} />;
  }
  if (focusedGateId === 5 || focusedGateId === 6) {
    return (
      <ContainerBoundarySvg
        gates={gates}
        gate6Result={gates.find(g => g.gate_id === 6)}
        gate5Result={gates.find(g => g.gate_id === 5)}
      />
    );
  }
  return null;
}
