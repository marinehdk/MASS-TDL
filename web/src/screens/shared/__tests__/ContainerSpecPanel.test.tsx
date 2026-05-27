import React from 'react';
import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ContainerSpecPanel } from '../ContainerSpecPanel';
import type { GateSSEEvent } from '../../../types/gateStream';

describe('ContainerSpecPanel', () => {
  it('renders nothing when focusedGateId does not match any container', () => {
    const { container } = render(<ContainerSpecPanel focusedGateId={null} />);
    expect(container.firstChild).toBeNull();
  });

  it('renders Gate 1 containers as active by default for sil-orchestrator-1', () => {
    render(<ContainerSpecPanel focusedGateId={1} gates={[]} />);
    expect(screen.getByText('SIL Orchestrator')).toBeDefined();
    // sil-orchestrator-1 and web-1 should be Active by definition
    expect(screen.getAllByText('Active').length).toBeGreaterThanOrEqual(1);
  });

  it('renders Foxglove Bridge and Martin Server as Offline when gate 1 data is missing or failed', () => {
    const failedGates: GateSSEEvent[] = [
      {
        gate_id: 1,
        label: 'System Readiness',
        passed: false,
        duration_ms: 100,
        rationale: 'fail',
        checks: [
          { item: 'foxglove_bridge:', status: 'fail', detail: 'foxglove_bridge: :8765 failed' },
          { item: 'martin', status: 'fail', detail: 'martin tile server: :3000 failed' }
        ]
      }
    ];
    render(<ContainerSpecPanel focusedGateId={1} gates={failedGates} />);
    expect(screen.getAllByText('Offline').length).toBe(2);
  });

  it('renders Foxglove Bridge and Martin Server as Running when port checks pass', () => {
    const passGates: GateSSEEvent[] = [
      {
        gate_id: 1,
        label: 'System Readiness',
        passed: true,
        duration_ms: 100,
        rationale: 'ok',
        checks: [
          { item: 'foxglove_bridge:', status: 'ok', detail: 'foxglove_bridge: :8765 listening' },
          { item: 'martin', status: 'ok', detail: 'martin tile server: :3000 listening' }
        ]
      }
    ];
    render(<ContainerSpecPanel focusedGateId={1} gates={passGates} />);
    expect(screen.getAllByText('Running').length).toBe(2);
  });

  it('renders SIL Nodes as Offline if Gate 1 DDS check is missing or not ok', () => {
    render(<ContainerSpecPanel focusedGateId={2} gates={[]} />);
    // Since gates is empty, DDS check is not 'ok', so SIL Nodes should be Offline
    expect(screen.getByText('Offline')).toBeDefined();
  });

  it('renders SIL Nodes as Active (blue indicator) when DDS is ok but all pulses are UNSPECIFIED', () => {
    const gates: GateSSEEvent[] = [
      {
        gate_id: 1,
        label: 'System Readiness',
        passed: true,
        duration_ms: 100,
        rationale: 'ok',
        checks: [
          { item: 'ROS2', status: 'ok', detail: 'ROS2 DDS: nodes visible' }
        ]
      },
      {
        gate_id: 2,
        label: 'Module Health',
        passed: true,
        duration_ms: 100,
        rationale: 'ok',
        checks: [
          { item: 'M1:', status: 'warn', detail: 'M1: UNSPECIFIED latency=0ms' },
          { item: 'M2:', status: 'warn', detail: 'M2: UNSPECIFIED latency=0ms' }
        ]
      }
    ];
    render(<ContainerSpecPanel focusedGateId={2} gates={gates} />);
    // SIL Nodes is active (blue indicator), Web UI is Active (green indicator)
    const activeStates = screen.getAllByText('Active');
    expect(activeStates.length).toBe(2); // One for SIL Nodes and one for Web UI
  });

  it('renders SIL Nodes as Running when DDS is ok and any pulse is GREEN/AMBER', () => {
    const gates: GateSSEEvent[] = [
      {
        gate_id: 1,
        label: 'System Readiness',
        passed: true,
        duration_ms: 100,
        rationale: 'ok',
        checks: [
          { item: 'ROS2', status: 'ok', detail: 'ROS2 DDS: nodes visible' }
        ]
      },
      {
        gate_id: 2,
        label: 'Module Health',
        passed: true,
        duration_ms: 100,
        rationale: 'ok',
        checks: [
          { item: 'M1:', status: 'ok', detail: 'M1: GREEN latency=2ms' },
          { item: 'M2:', status: 'warn', detail: 'M2: UNSPECIFIED latency=0ms' }
        ]
      }
    ];
    render(<ContainerSpecPanel focusedGateId={2} gates={gates} />);
    expect(screen.getByText('Running')).toBeDefined();
    expect(screen.getByText('Active')).toBeDefined(); // Web UI should still be Active
  });
});
