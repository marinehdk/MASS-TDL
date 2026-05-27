import React from 'react';
import type { GateSSEEvent } from '../../types/gateStream';

const CONTAINERS = [
  { name: 'SIL Orchestrator', container: 'sil-orchestrator-1', image: 'mass-l3-tacticallayer-sil-orchestrator:latest', desc: 'FastAPI 后端, 场景与仿真API', port: '8000', gateId: 1 },
  { name: 'Foxglove Bridge', container: 'foxglove-bridge-1', image: 'mass-l3/ci:humble-ubuntu22.04', desc: '高频通信 WebSocket 桥接', port: '8765', gateId: 1 },
  { name: 'Martin Server', container: 'martin-tile-server-1', image: 'ghcr.io/maplibre/martin:latest', desc: '离线海图矢量瓦片服务', port: '3000', gateId: 1 },
  
  { name: 'SIL Nodes', container: 'sil-nodes-1', image: 'mass-l3-tacticallayer-sil-nodes:latest', desc: 'ROS2 核心仿真逻辑节点', port: 'Host Mode', gateId: 2 },
  { name: 'Web UI', container: 'web-1', image: 'mass-l3-tacticallayer-web:latest', desc: '3-Pane Studio 前端界面', port: '5173', gateId: 2 }
];

interface ContainerSpecPanelProps {
  focusedGateId: number | null;
  gates?: GateSSEEvent[];
}

const STATUS_STYLES = {
  running: {
    bg: 'rgba(16, 185, 129, 0.08)',
    border: '1px solid rgba(16, 185, 129, 0.2)',
    borderColor: 'rgba(16, 185, 129, 0.25)',
    color: '#10B981',
    pulseClass: 'pulse-dot-green'
  },
  active: {
    bg: 'rgba(59, 130, 246, 0.08)',
    border: '1px solid rgba(59, 130, 246, 0.2)',
    borderColor: 'rgba(59, 130, 246, 0.25)',
    color: '#3B82F6',
    pulseClass: 'pulse-dot-blue'
  },
  offline: {
    bg: 'rgba(239, 68, 68, 0.08)',
    border: '1px solid rgba(239, 68, 68, 0.2)',
    borderColor: 'rgba(239, 68, 68, 0.25)',
    color: '#EF4444',
    pulseClass: 'pulse-dot-red'
  }
};

export function ContainerSpecPanel({ focusedGateId, gates }: ContainerSpecPanelProps) {
  // Filter based on selected gate (Gate 1 gets the 3 infra containers, Gate 2 gets the 2 module containers)
  const filteredContainers = CONTAINERS.filter(c => c.gateId === focusedGateId);

  if (filteredContainers.length === 0) return null;

  const getContainerStatus = (container: string): { state: 'running' | 'active' | 'offline'; label: string } => {
    if (container === 'sil-orchestrator-1' || container === 'web-1') {
      return { state: 'running', label: 'Active' };
    }

    const gate1 = gates?.find(g => g.gate_id === 1);

    if (container === 'foxglove-bridge-1') {
      const check = gate1?.checks?.find(c => c.item?.includes(':8765') || c.detail?.includes(':8765'));
      const isOk = check?.status === 'ok';
      return isOk 
        ? { state: 'running', label: 'Running' } 
        : { state: 'offline', label: 'Offline' };
    }

    if (container === 'martin-tile-server-1') {
      const check = gate1?.checks?.find(c => c.item?.includes(':3000') || c.detail?.includes(':3000'));
      const isOk = check?.status === 'ok';
      return isOk 
        ? { state: 'running', label: 'Running' } 
        : { state: 'offline', label: 'Offline' };
    }

    if (container === 'sil-nodes-1') {
      const ddsCheck = gate1?.checks?.find(c => c.item?.toUpperCase() === 'ROS2' || c.detail?.toUpperCase().includes('DDS'));
      const ddsOk = ddsCheck?.status === 'ok';
      if (!ddsOk) {
        return { state: 'offline', label: 'Offline' };
      }
      
      const gate2 = gates?.find(g => g.gate_id === 2);
      const mChecks = gate2?.checks?.filter(c => /^M\d+:?$/.test(c.item)) || [];
      const anyGreenAmber = mChecks.some(c => {
        const detailUpper = (c.detail || '').toUpperCase();
        return detailUpper.includes('GREEN') || detailUpper.includes('AMBER');
      });

      if (anyGreenAmber) {
        return { state: 'running', label: 'Running' };
      }
      
      return { state: 'active', label: 'Active' };
    }

    return { state: 'offline', label: 'Offline' };
  };

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
      <style>{`
        @keyframes pulse-green {
          0% { box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.7); }
          70% { box-shadow: 0 0 0 6px rgba(16, 185, 129, 0); }
          100% { box-shadow: 0 0 0 0 rgba(16, 185, 129, 0); }
        }
        @keyframes pulse-blue {
          0% { box-shadow: 0 0 0 0 rgba(59, 130, 246, 0.7); }
          70% { box-shadow: 0 0 0 6px rgba(59, 130, 246, 0); }
          100% { box-shadow: 0 0 0 0 rgba(59, 130, 246, 0); }
        }
        @keyframes pulse-red {
          0% { box-shadow: 0 0 0 0 rgba(239, 68, 68, 0.7); }
          70% { box-shadow: 0 0 0 6px rgba(239, 68, 68, 0); }
          100% { box-shadow: 0 0 0 0 rgba(239, 68, 68, 0); }
        }
        .pulse-dot {
          display: inline-block;
          width: 7px;
          height: 7px;
          border-radius: 50%;
          margin-right: 6px;
        }
        .pulse-dot-green {
          animation: pulse-green 1.8s infinite;
          background-color: #10B981;
        }
        .pulse-dot-blue {
          animation: pulse-blue 1.8s infinite;
          background-color: #3B82F6;
        }
        .pulse-dot-red {
          animation: pulse-red 1.8s infinite;
          background-color: #EF4444;
        }
      `}</style>

      <div>
        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--c-stbd)', background: 'rgba(0,227,179,0.08)', border: '1px solid rgba(0,227,179,0.2)', padding: '8px 12px', borderRadius: 4, wordBreak: 'break-all', fontWeight: 600 }}>
          编译文件：./docker-compose.yml
        </div>
      </div>
      
      <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
        {filteredContainers.map(c => {
          const { state, label } = getContainerStatus(c.container);
          const statusStyles = STATUS_STYLES[state];

          return (
            <div key={c.container} style={{
              border: `1px solid ${statusStyles.borderColor}`,
              borderRadius: 6,
              padding: '12px 14px',
              background: 'var(--bg-0)',
              boxShadow: '0 2px 6px rgba(0,0,0,0.15)',
              display: 'flex',
              flexDirection: 'column',
              gap: 8,
              transition: 'all 0.2s ease-in-out'
            }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span style={{ fontFamily: 'var(--f-disp)', fontSize: 14.5, color: 'var(--txt-0)', fontWeight: 700 }}>
                  {c.name}
                </span>
                
                <div style={{
                  display: 'inline-flex',
                  alignItems: 'center',
                  padding: '3px 8px',
                  borderRadius: 12,
                  background: statusStyles.bg,
                  border: statusStyles.border,
                  color: statusStyles.color,
                  fontSize: 11,
                  fontWeight: 600,
                  fontFamily: 'var(--f-mono)',
                  lineHeight: '12px'
                }}>
                  <span className={`pulse-dot ${statusStyles.pulseClass}`} />
                  {label}
                </div>
              </div>

              <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11.5, color: 'var(--txt-1)', display: 'flex', justifyContent: 'space-between' }}>
                  <span>容器：<span style={{ color: 'var(--txt-0)' }}>{c.container}</span></span>
                  <span style={{ color: 'var(--c-info)', fontWeight: 600 }}>
                    {c.port === 'Host Mode' ? '网络模式 Host' : `端口 ${c.port}`}
                  </span>
                </div>
                
                <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11.5, color: 'var(--txt-2)', wordBreak: 'break-all' }}>
                  镜像：{c.image}
                </div>
              </div>

              <div style={{
                fontFamily: 'var(--f-body)',
                fontSize: 12.5,
                color: 'var(--txt-1)',
                marginTop: 4,
                borderTop: '1px dashed var(--line-2)',
                paddingTop: 6
              }}>
                功能：{c.desc}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}

