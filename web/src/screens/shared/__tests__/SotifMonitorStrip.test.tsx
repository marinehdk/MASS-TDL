import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { SotifMonitorStrip } from '../SotifMonitorStrip';
import type { SotifMetrics } from '../../../types/sat';

const nominal: SotifMetrics = {
  ais_radar_consistency_sigma: 1.8,
  target_predictability_rms_m: 41,
  perception_coverage_pct: 95,
  colregs_parse_failures: 0,
  comm_link_rtt_ms: 120,
  checker_veto_rate_pct: 8,
};

const violated: SotifMetrics = {
  ais_radar_consistency_sigma: 2.5,
  target_predictability_rms_m: 41,
  perception_coverage_pct: 75,
  colregs_parse_failures: 0,
  comm_link_rtt_ms: 120,
  checker_veto_rate_pct: 8,
};

describe('SotifMonitorStrip', () => {
  it('renders null when metrics is null', () => {
    const { container } = render(<SotifMonitorStrip metrics={null} />);
    expect(container.firstChild).toBeNull();
  });

  it('renders 6 rows for 6 assumptions', () => {
    render(<SotifMonitorStrip metrics={nominal} />);
    const rows = document.querySelectorAll('[data-testid^="sotif-row-"]');
    expect(rows).toHaveLength(6);
  });

  it('all rows green when nominal', () => {
    render(<SotifMonitorStrip metrics={nominal} />);
    const rows = document.querySelectorAll('[data-testid^="sotif-row-"]');
    rows.forEach((row) => {
      expect(row.getAttribute('data-violated')).toBe('false');
    });
  });

  it('AIS consistency row violated when sigma > 2.0', () => {
    render(<SotifMonitorStrip metrics={violated} />);
    expect(document.querySelector('[data-testid="sotif-row-ais"]')?.getAttribute('data-violated')).toBe('true');
  });

  it('perception coverage row violated when < 80%', () => {
    render(<SotifMonitorStrip metrics={violated} />);
    expect(document.querySelector('[data-testid="sotif-row-coverage"]')?.getAttribute('data-violated')).toBe('true');
  });

  it('shows MRM recommendation when any row violated', () => {
    render(<SotifMonitorStrip metrics={violated} />);
    expect(screen.getByText(/MRM-01/)).toBeInTheDocument();
  });
});
