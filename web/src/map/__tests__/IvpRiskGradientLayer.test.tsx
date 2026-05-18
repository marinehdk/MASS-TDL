import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { IvpRiskGradientLayer } from '../IvpRiskGradientLayer';
import type { IvpContribution } from '../../types/sat';

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

describe('IvpRiskGradientLayer', () => {
  it('renders null when contributions empty', () => {
    const { container } = render(
      <IvpRiskGradientLayer contributions={[]} activeBehavior={null} activeBehaviorWeight={0} ownShipScreenPos={[400, 300]} headingDeg={0} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('renders 8 arrows for 8 contributions', () => {
    render(
      <IvpRiskGradientLayer contributions={contributions} activeBehavior="COLREGs_Avoidance" activeBehaviorWeight={0.7} ownShipScreenPos={[400, 300]} headingDeg={45} />
    );
    const arrows = document.querySelectorAll('[data-testid^="ivp-arrow-"]');
    expect(arrows).toHaveLength(8);
  });

  it('high-cost direction has red fill', () => {
    render(
      <IvpRiskGradientLayer contributions={contributions} activeBehavior={null} activeBehaviorWeight={0} ownShipScreenPos={[400, 300]} headingDeg={0} />
    );
    const arrow90 = document.querySelector('[data-testid="ivp-arrow-90"]');
    expect(arrow90?.getAttribute('fill')).toBe('#f87171');
  });

  it('shows active behavior label', () => {
    render(
      <IvpRiskGradientLayer contributions={contributions} activeBehavior="COLREGs_Avoidance" activeBehaviorWeight={0.7} ownShipScreenPos={[400, 300]} headingDeg={0} />
    );
    expect(screen.getByText(/COLREGs_Avoidance/)).toBeInTheDocument();
  });
});
