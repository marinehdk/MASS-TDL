import { describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { DecisionEventMarkers } from '../DecisionEventMarkers';

describe('DecisionEventMarkers', () => {
  it('positions markers by event time over duration', () => {
    render(<DecisionEventMarkers events={[{ t: 30, k: 'M6_RULE_ASSERTED', sev: 'warn', m: 'M6', d: 'Rule 15' }]} durationSec={120} />);

    expect(screen.getByTestId('decision-event-M6_RULE_ASSERTED')).toHaveStyle({ left: '25%' });
  });

  it('uses red marker for critical safety event', () => {
    render(<DecisionEventMarkers events={[{ t: 50, k: 'M7_SAFETY_ALERT', sev: 'crit', m: 'M7', d: 'MRM-03' }]} durationSec={100} />);

    expect(screen.getByTestId('decision-event-M7_SAFETY_ALERT')).toHaveAttribute('data-severity', 'crit');
    expect(screen.getByTestId('decision-event-M7_SAFETY_ALERT')).toHaveTextContent('M7');
  });

  it('calls onSelectTime when marker is clicked', () => {
    const onSelectTime = vi.fn();
    render(
      <DecisionEventMarkers
        events={[{ t: 80, k: 'CLEAR_RETURN', sev: 'info', m: 'M8', d: 'clear' }]}
        durationSec={100}
        onSelectTime={onSelectTime}
      />,
    );

    fireEvent.click(screen.getByTestId('decision-event-CLEAR_RETURN'));

    expect(onSelectTime).toHaveBeenCalledWith(80);
  });

  it('renders nothing when duration is zero', () => {
    const { container } = render(<DecisionEventMarkers events={[{ t: 1, k: 'M6_RULE_ASSERTED', sev: 'warn', m: 'M6', d: 'Rule 15' }]} durationSec={0} />);

    expect(container).toBeEmptyDOMElement();
  });
});
