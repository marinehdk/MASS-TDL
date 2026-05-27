import React, { useState } from 'react';
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

export function ContainerSpecPanel({ focusedGateId, gates }: ContainerSpecPanelProps) {
  // Filter based on selected gate (Gate 1 gets the 3 infra containers, Gate 2 gets the 2 module containers)
  const filteredContainers = CONTAINERS.filter(c => c.gateId === focusedGateId);

  if (filteredContainers.length === 0) return null;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
      <div>
        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--c-stbd)', background: 'rgba(0,227,179,0.08)', border: '1px solid rgba(0,227,179,0.2)', padding: '8px 12px', borderRadius: 4, wordBreak: 'break-all', fontWeight: 600 }}>
          编译文件：./docker-compose.yml
        </div>
      </div>
      
      <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
        {filteredContainers.map(c => (
          <div key={c.container} style={{ border: '1px solid var(--line-2)', borderRadius: 6, padding: '12px 14px', background: 'var(--bg-0)', boxShadow: '0 2px 6px rgba(0,0,0,0.15)', display: 'flex', flexDirection: 'column', gap: 6 }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 2 }}>
              <span style={{ fontFamily: 'var(--f-disp)', fontSize: 14.5, color: 'var(--txt-0)', fontWeight: 700 }}>{c.name}</span>
              <span style={{ fontFamily: 'var(--f-mono)', fontSize: 12, fontWeight: 700, color: 'var(--c-info)' }}>
                {c.port === 'Host Mode' ? '网络模式 Host' : `端口 ${c.port}`}
              </span>
            </div>
            <div style={{ fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--txt-1)' }}>
              容器名称：{c.container}
            </div>
            <div style={{ fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--txt-1)', wordBreak: 'break-all' }}>
              镜像名称：{c.image}
            </div>
            <div style={{ fontFamily: 'var(--f-body)', fontSize: 13, color: 'var(--txt-1)', marginTop: 4, borderTop: '1px dashed var(--line-2)', paddingTop: 6 }}>
              功能：{c.desc}
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
