import { useState } from 'react';
import type { GateSSEEvent } from '../../types/gateStream';
import {
  useRestartServicesMutation, useRestartNodeMutation, useSyncTimeMutation,
  useClearHashCacheMutation, useEnsureAsdrDirMutation,
} from '../../api/silApi';

interface FixRowProps {
  label: string;
  buttonLabel: string;
  actionId: string;
  running: string | null;
  onClick: () => void;
  variant?: 'danger';
}

function FixRow({ label, buttonLabel, actionId, running, onClick, variant }: FixRowProps) {
  return (
    <div style={{
      display: 'flex',
      justifyContent: 'space-between',
      alignItems: 'center',
      padding: '8px 0',
      borderBottom: '1px solid rgba(255,255,255,0.05)',
      gap: 12
    }}>
      <span style={{
        fontFamily: 'var(--f-body)',
        fontSize: 13,
        color: 'var(--txt-1)',
        fontWeight: 600
      }}>
        {label}
      </span>
      <FixButton 
        label={buttonLabel} 
        actionId={actionId} 
        running={running} 
        onClick={onClick} 
        variant={variant} 
      />
    </div>
  );
}

interface QuickFixPanelProps {
  focusedGateId: number | null;
  gates: Array<{ gate_id: number; passed: boolean }>;
  scenarioId?: string | null;
  runId?: string | null;
  onFixApplied: () => void;
  isEmbed?: boolean;
}

export function QuickFixPanel({ focusedGateId, gates, scenarioId, runId, onFixApplied, isEmbed }: QuickFixPanelProps) {
  const [restartServices] = useRestartServicesMutation();
  const [restartNode] = useRestartNodeMutation();
  const [syncTime] = useSyncTimeMutation();
  const [clearHashCache] = useClearHashCacheMutation();
  const [ensureAsdrDir] = useEnsureAsdrDirMutation();
  const [runningAction, setRunningAction] = useState<string | null>(null);
  const [lastResult, setLastResult] = useState<string | null>(null);

  // File Modal States
  const [showComposeModal, setShowComposeModal] = useState(false);
  const [composeContent, setComposeContent] = useState('');
  const [composeLoading, setComposeLoading] = useState(false);

  const failedGateIds = gates.filter(g => !g.passed).map(g => g.gate_id);

  async function execute(label: string, fn: () => Promise<unknown>) {
    setRunningAction(label);
    setLastResult(null);
    try {
      const res = await fn();
      setLastResult(typeof res === 'object' && res !== null && 'data' in res
        ? JSON.stringify((res as any).data) : 'OK');
      onFixApplied();
    } catch (e) {
      setLastResult(`FAILED: ${(e as Error).message}`);
    } finally {
      setRunningAction(null);
    }
  }

  async function openComposeFile() {
    setComposeLoading(true);
    try {
      const res = await fetch('/api/v1/ops/compose_content');
      const data = await res.json();
      if (data.success) {
        setComposeContent(data.content);
        setShowComposeModal(true);
      } else {
        alert('无法加载 docker-compose.yml: ' + data.message);
      }
    } catch (e) {
      alert('加载失败: ' + (e as Error).message);
    } finally {
      setComposeLoading(false);
    }
  }

  if (!focusedGateId || failedGateIds.length === 0) return null;

  const content = (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
      {focusedGateId === 1 && (
        <>
          <FixRow
            label="编译文件 (docker-compose.yml)"
            buttonLabel={composeLoading ? '读取中' : '打开'}
            actionId="Open Compose File"
            running={composeLoading ? 'Open Compose File' : null}
            onClick={openComposeFile}
          />
          <FixRow
            label="SIL Orchestrator"
            buttonLabel="重启"
            actionId="重启 SIL Orchestrator"
            running={runningAction}
            onClick={() => execute('重启 SIL Orchestrator', () => restartNode('sil-orchestrator-1'))}
          />
          <FixRow
            label="Foxglove Bridge"
            buttonLabel="重启"
            actionId="重启 Foxglove Bridge"
            running={runningAction}
            onClick={() => execute('重启 Foxglove Bridge', () => restartNode('foxglove-bridge-1'))}
          />
          <FixRow
            label="Martin Server"
            buttonLabel="重启"
            actionId="重启 Martin Server"
            running={runningAction}
            onClick={() => execute('重启 Martin Server', () => restartNode('martin-tile-server-1'))}
          />
        </>
      )}

      {focusedGateId === 2 && (
        <FixRow
          label="全部模块容器"
          buttonLabel="重启"
          actionId="Restart Modules"
          running={runningAction}
          onClick={() => execute('Restart Modules', () => restartServices())}
        />
      )}

      {focusedGateId === 3 && scenarioId && (
        <FixRow
          label="场景 Hash 缓存"
          buttonLabel="清除"
          actionId="Clear Cache"
          running={runningAction}
          onClick={() => execute('Clear Cache', () => clearHashCache(scenarioId!))}
        />
      )}

      {focusedGateId === 4 && (
        <FixRow
          label="M1 ODD 配置"
          buttonLabel="重载"
          actionId="Reload M1"
          running={runningAction}
          onClick={() => execute('Reload M1', () => restartNode('m1_*'))}
        />
      )}

      {focusedGateId === 5 && (
        <>
          <FixRow
            label="PTP 授时时钟"
            buttonLabel="同步"
            actionId="Sync Time"
            running={runningAction}
            onClick={() => execute('Sync Time', () => syncTime())}
          />
          <FixRow
            label="ASDR 证据链目录"
            buttonLabel="创建"
            actionId="Ensure ASDR"
            running={runningAction}
            onClick={() => execute('Ensure ASDR', () => ensureAsdrDir(runId ?? 'unknown'))}
          />
        </>
      )}

      {focusedGateId === 6 && (
        <FixRow
          label="M7 隔离区容器"
          buttonLabel="重启"
          actionId="Restart M7"
          running={runningAction}
          onClick={() => execute('Restart M7', () => restartNode('m7_*'))}
        />
      )}

      <div style={{ marginTop: 8, borderTop: '1px dashed var(--line-2)', paddingTop: 8 }}>
        <FixRow
          label="全局重新配置 (所有服务)"
          buttonLabel="强制重置"
          actionId="Global Reconfigure"
          running={runningAction}
          onClick={() => execute('Global Reconfigure', () => restartServices())}
          variant="danger"
        />
      </div>

      {lastResult && (
        <div style={{ marginTop: 6, padding: '4px 8px', background: 'var(--bg-2)', borderRadius: 3, fontSize: 10, fontFamily: 'var(--f-mono)', color: 'var(--txt-2)', wordBreak: 'break-all' }}>
          {lastResult}
        </div>
      )}

      {/* Premium docker-compose.yml YAML Viewer Modal */}
      {showComposeModal && (
        <div style={{
          position: 'fixed',
          top: 0,
          left: 0,
          right: 0,
          bottom: 0,
          background: 'rgba(5, 8, 15, 0.94)',
          backdropFilter: 'blur(10px)',
          zIndex: 10000,
          display: 'flex',
          justifyContent: 'center',
          alignItems: 'center',
          padding: 40
        }}>
          <div style={{
            width: '85%',
            maxWidth: 1000,
            height: '80vh',
            background: 'var(--bg-1)',
            border: '1px solid var(--line-1)',
            borderRadius: 12,
            display: 'flex',
            flexDirection: 'column',
            overflow: 'hidden',
            boxShadow: '0 20px 50px rgba(0,0,0,0.6)'
          }}>
            {/* Modal Header */}
            <div style={{
              display: 'flex',
              justifyContent: 'space-between',
              alignItems: 'center',
              padding: '16px 24px',
              borderBottom: '1px solid var(--line-1)',
              background: 'rgba(0, 227, 179, 0.05)'
            }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                <div style={{ width: 6, height: 16, background: 'var(--c-phos)', borderRadius: 2 }} />
                <span style={{ fontFamily: 'var(--f-disp)', fontSize: 15, fontWeight: 700, color: 'var(--txt-1)', letterSpacing: '0.05em' }}>
                  docker-compose.yml 编译文件查看器
                </span>
              </div>
              <button 
                onClick={() => setShowComposeModal(false)}
                style={{
                  background: 'transparent',
                  border: '1px solid var(--line-2)',
                  color: 'var(--txt-1)',
                  padding: '5px 14px',
                  borderRadius: 4,
                  cursor: 'pointer',
                  fontSize: 11,
                  fontFamily: 'var(--f-disp)',
                  fontWeight: 700,
                  transition: 'all 0.2s'
                }}
                onMouseOver={(e) => { e.currentTarget.style.background = 'rgba(255,255,255,0.08)'; }}
                onMouseOut={(e) => { e.currentTarget.style.background = 'transparent'; }}
              >
                关闭窗口
              </button>
            </div>
            {/* Modal Content */}
            <div style={{
              flex: 1,
              overflowY: 'auto',
              padding: 24,
              fontFamily: 'var(--f-mono)',
              fontSize: 12.5,
              lineHeight: 1.6,
              color: 'var(--txt-1)',
              background: '#070b12',
              whiteSpace: 'pre-wrap',
              textAlign: 'left'
            }}>
              {composeContent}
            </div>
          </div>
        </div>
      )}
    </div>
  );

  if (isEmbed) return content;

  return (
    <div style={{ padding: '12px 16px', borderTop: '1px solid var(--line-2)', background: 'var(--bg-1)' }}>
      <div style={{ fontFamily: 'var(--f-disp)', fontSize: 12, color: 'var(--txt-0)', marginBottom: 8 }}>QUICK FIX</div>
      {content}
    </div>
  );
}


function FixButton({ label, actionId, running, onClick, variant }: {
  label: string; actionId: string; running: string | null; onClick: () => void; variant?: 'danger';
}) {
  const isActive = running === actionId;
  return (
    <button onClick={onClick} disabled={running !== null} style={{
      padding: '5px 14px', border: 'none', borderRadius: 4, cursor: running ? 'not-allowed' : 'pointer',
      background: variant === 'danger' ? 'rgba(248,81,73,0.15)' : 'var(--c-phos)',
      color: variant === 'danger' ? 'var(--c-danger)' : '#000',
      fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 700, opacity: running ? 0.5 : 1,
      minWidth: 70, textAlign: 'center', transition: 'all 0.15s'
    }}
      onMouseOver={(e) => { if (!running) e.currentTarget.style.filter = 'brightness(1.1)'; }}
      onMouseOut={(e) => { if (!running) e.currentTarget.style.filter = 'none'; }}
    >
      {isActive ? '运行中' : label}
    </button>
  );
}
