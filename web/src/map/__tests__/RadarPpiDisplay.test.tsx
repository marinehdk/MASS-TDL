import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import React from 'react';
import { RadarPpiDisplay } from '../RadarPpiDisplay';
import type { OwnShipState, TargetVesselState } from '../../types';

describe('RadarPpiDisplay', () => {
  const mockOwnShip: OwnShipState = {
    mmsi: 999999999,
    pose: {
      lat: 63.4,
      lon: 10.4,
      heading: (90 * Math.PI) / 180, // Heading East
    },
    kinematics: {
      sog: 10.0,
      cog: (90 * Math.PI) / 180,
      rot: 0,
      u: 0,
      v: 0,
      r: 0,
    },
  } as any;

  const mockTargets: TargetVesselState[] = [
    {
      mmsi: 100000001,
      pose: {
        lat: 63.41, // Slightly North (~0.6 NM)
        lon: 10.4,  // Same Lon
        heading: 0,
      },
      kinematics: {
        sog: 12.0,
        cog: 0,
        rot: 0,
      },
      shipType: 1,
      mode: 3,
    },
    {
      mmsi: 100000002,
      pose: {
        lat: 63.4,
        lon: 10.45, // Slightly East (~1.3 NM adjusted by cos lat)
        heading: (90 * Math.PI) / 180,
      },
      kinematics: {
        sog: 8.0,
        cog: (90 * Math.PI) / 180,
        rot: 0,
      },
      shipType: 1,
      mode: 3,
    },
  ];

  it('renders "NO TELEMETRY" when ownShip is null', () => {
    render(<RadarPpiDisplay ownShip={null} targets={[]} relativeMode={false} />);
    expect(screen.getByText('NO TELEMETRY')).toBeInTheDocument();
    expect(screen.getByText('STANDBY SCANNER')).toBeInTheDocument();
  });

  it('renders Radar Grid and own ship when ownShip is present', () => {
    render(<RadarPpiDisplay ownShip={mockOwnShip} targets={[]} relativeMode={false} />);
    expect(screen.getByTestId('radar-ppi-display')).toBeInTheDocument();
    expect(screen.queryByText('NO TELEMETRY')).not.toBeInTheDocument();
    expect(screen.queryByText('1.0NM')).not.toBeInTheDocument();
    expect(screen.queryByText('2.0NM')).not.toBeInTheDocument();
    expect(screen.queryByText('3.0NM')).not.toBeInTheDocument();
  });

  it('filters out targets beyond 3.0 NM range and renders nearby targets', () => {
    // A target 1 degree latitude away (60 NM) is far out of 3.0 NM range
    const farTarget: TargetVesselState = {
      mmsi: 100000003,
      pose: {
        lat: 64.4,
        lon: 10.4,
        heading: 0,
      },
      kinematics: { sog: 5, cog: 0, rot: 0 },
      shipType: 1,
      mode: 3,
    };

    render(
      <RadarPpiDisplay
        ownShip={mockOwnShip}
        targets={[...mockTargets, farTarget]}
        relativeMode={false}
      />
    );

    // Nearby targets should be displayed
    expect(screen.getByText('TS 001')).toBeInTheDocument(); // last 3 digits of 100000001
    expect(screen.getByText('TS 002')).toBeInTheDocument(); // last 3 digits of 100000002
    expect(screen.getByText('TS 001')).toHaveAttribute('text-anchor', 'middle');

    // Far target should be filtered out
    expect(screen.queryByText('TS 003')).not.toBeInTheDocument();
  });

  it('correctly labels the mode overlay tag', () => {
    const { rerender } = render(
      <RadarPpiDisplay ownShip={mockOwnShip} targets={[]} relativeMode={true} />
    );
    expect(screen.getByText('H-UP (REL)')).toBeInTheDocument();

    rerender(<RadarPpiDisplay ownShip={mockOwnShip} targets={[]} relativeMode={false} />);
    expect(screen.getByText('N-UP (TRUE)')).toBeInTheDocument();
  });

  it('can render an expanded 12 NM tactical range with sparse 2/6/10 NM ring labels', () => {
    render(
      <RadarPpiDisplay
        ownShip={mockOwnShip}
        targets={mockTargets}
        relativeMode={false}
        size={460}
        maxRangeNM={12}
        rangeRingsNM={[2, 6, 10]}
      />
    );

    const radar = screen.getByTestId('radar-ppi-display');
    expect(radar).toHaveStyle({ width: '460px', height: '460px' });
    expect(screen.getByText('2nm')).toBeInTheDocument();
    expect(screen.getByText('6nm')).toBeInTheDocument();
    expect(screen.getByText('10nm')).toBeInTheDocument();
    expect(screen.queryByText('4nm')).not.toBeInTheDocument();
    expect(screen.queryByText('8nm')).not.toBeInTheDocument();
    expect(screen.queryByText('12nm')).not.toBeInTheDocument();
  });

  it('computes a distinct CPA point when backend CPA metrics are absent', () => {
    render(
      <RadarPpiDisplay
        ownShip={mockOwnShip}
        targets={[mockTargets[0]]}
        relativeMode={false}
        size={460}
        maxRangeNM={12}
        rangeRingsNM={[2, 6, 10]}
      />
    );

    const target = screen.getByTestId('radar-target-100000001');
    const cpaPoint = screen.getByTestId('radar-cpa-point');
    expect(cpaPoint).toBeInTheDocument();
    expect(cpaPoint.getAttribute('cy')).not.toBe(target.getAttribute('cy'));
  });
});
