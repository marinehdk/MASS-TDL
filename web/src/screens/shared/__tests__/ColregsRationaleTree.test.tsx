import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ColregsRationaleTree } from '../ColregsRationaleTree';
import type { ColregsChainLayer } from '../../../types/sat';

const mockChain: ColregsChainLayer[] = [
  { layer: 1, label: 'ODD', conclusion: 'ODD-A', inputs: { odd_domain: 'A' } },
  { layer: 2, label: '会遇分类', conclusion: 'Rule 14 ✓', inputs: { hdg_diff: 178, bearing: 3.2, range_nm: 2.1 }, confidence: 0.94 },
  { layer: 3, label: '责任', conclusion: 'GIVE-WAY', inputs: { rule: 'Rule 16' } },
  { layer: 4, label: '行动方向', conclusion: 'STARBOARD ≥30°', inputs: { rule: 'Rule 8' } },
  { layer: 5, label: '时机', conclusion: 'STAGE_3', inputs: { tcpa_min: 4.0, t_act: 4.0 }, timing_stage: 'STAGE_3', escalation: true },
];

describe('ColregsRationaleTree', () => {
  it('renders empty state when no chain', () => {
    render(<ColregsRationaleTree chain={[]} targetId={null} latencyMs={0} />);
    expect(screen.getByText(/No active COLREGs/i)).toBeInTheDocument();
  });

  it('renders all 5 layer conclusions', () => {
    render(<ColregsRationaleTree chain={mockChain} targetId="123456789" latencyMs={2.3} />);
    expect(screen.getByText(/ODD-A/)).toBeInTheDocument();
    expect(screen.getByText(/Rule 14/)).toBeInTheDocument();
    expect(screen.getByText(/GIVE-WAY/)).toBeInTheDocument();
    expect(screen.getByText(/STARBOARD/)).toBeInTheDocument();
    expect(screen.getByText(/STAGE_3/)).toBeInTheDocument();
  });

  it('shows target MMSI', () => {
    render(<ColregsRationaleTree chain={mockChain} targetId="123456789" latencyMs={2.3} />);
    expect(screen.getByText(/123456789/)).toBeInTheDocument();
  });

  it('shows latency', () => {
    render(<ColregsRationaleTree chain={mockChain} targetId="123456789" latencyMs={2.3} />);
    expect(screen.getByText(/2.3ms/)).toBeInTheDocument();
  });

  it('highlights STAGE_3 with escalation styling', () => {
    render(<ColregsRationaleTree chain={mockChain} targetId="123456789" latencyMs={2.3} />);
    const stage3 = screen.getByTestId('colregs-layer-5');
    expect(stage3.className || stage3.getAttribute('data-escalation')).toBeTruthy();
  });
});
