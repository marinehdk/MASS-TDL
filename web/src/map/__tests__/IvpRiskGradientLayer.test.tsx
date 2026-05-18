import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { useRef } from 'react';
import { IvpRiskGradientLayer } from '../IvpRiskGradientLayer';
import type { IvpContribution } from '../../types/sat';

const mockProject = vi.fn(() => ({ x: 400, y: 300 }));
const mockMap = {
  on: vi.fn(),
  off: vi.fn(),
  project: mockProject,
};

vi.mock('maplibre-gl', () => ({
  default: { Map: vi.fn(() => mockMap) },
}));

const contributions: IvpContribution[] = [
  { direction_deg: 0,   cost: 0.2 },
  { direction_deg: 45,  cost: 0.5 },
  { direction_deg: 90,  cost: 0.9 },
  { direction_deg: 135, cost: 0.1 },
  { direction_deg: 180, cost: 0.3 },
  { direction_deg: 225, cost: 0.4 },
  { direction_deg: 270, cost: 0.7 },
  { direction_deg: 315, cost: 0.6 },
];

const mockOwnShip = {
  pose: { lon: 10.4, lat: 63.4, heading: 0 },
  kinematics: { sog: 0, cog: 0, rot: 0 },
} as any;

function Wrapper({ conts, behavior, weight }: { conts: IvpContribution[]; behavior: string | null; weight: number }) {
  const ref = useRef(mockMap as any);
  return (
    <IvpRiskGradientLayer
      mapRef={ref}
      ownShip={mockOwnShip}
      contributions={conts}
      activeBehavior={behavior}
      activeBehaviorWeight={weight}
    />
  );
}

describe('IvpRiskGradientLayer', () => {
  it('renders null when contributions empty', () => {
    const { container } = render(<Wrapper conts={[]} behavior={null} weight={0} />);
    expect(container.firstChild).toBeNull();
  });

  it('renders 8 arrows for 8 contributions', () => {
    render(<Wrapper conts={contributions} behavior="COLREGs_Avoidance" weight={0.7} />);
    const arrows = document.querySelectorAll('[data-testid^="ivp-arrow-"]');
    expect(arrows).toHaveLength(8);
  });

  it('high-cost direction has red stroke', () => {
    render(<Wrapper conts={contributions} behavior={null} weight={0} />);
    const arrow90 = document.querySelector('[data-testid="ivp-arrow-90"]');
    expect(arrow90?.getAttribute('stroke')).toBe('#f87171');
  });

  it('shows active behavior label', () => {
    render(<Wrapper conts={contributions} behavior="COLREGs_Avoidance" weight={0.7} />);
    expect(screen.getByText(/COLREGs_Avoidance/)).toBeInTheDocument();
  });
});
