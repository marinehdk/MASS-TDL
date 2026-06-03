import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import React from 'react';
import { AsdrLedger } from '../AsdrLedger';

const makeMockEvents = (count: number) => {
  const list = [];
  for (let i = 0; i < count; i++) {
    const minutes = Math.floor(i / 60);
    const seconds = i % 60;
    const timeStr = `T+${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
    list.push({
      time: timeStr,
      type: i % 10 === 0 ? 'CRIT_FAIL' : i % 5 === 0 ? 'WARN_SLOW' : 'INFO_OK',
      module: 'M1',
      payload: { msg: `Event ${i}` },
      hash: `hash_${i}`,
    });
  }
  return list;
};

describe('AsdrLedger - Time Syncing and Highlighting', () => {
  const scrollMock = vi.fn();

  beforeEach(() => {
    // Mock scrollIntoView since jsdom doesn't implement it
    HTMLElement.prototype.scrollIntoView = scrollMock;
    scrollMock.mockClear();
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('renders standard table and pagination correctly', () => {
    const events = makeMockEvents(120);
    render(<AsdrLedger events={events} />);

    // Page 1 should show events 1-50
    expect(screen.getByText('1/3')).toBeDefined();
    // Header should show count
    expect(screen.getByText('ASDR LEDGER (120)')).toBeDefined();
  });

  it('does not highlight any row when currentTimeSec is undefined', () => {
    const events = makeMockEvents(10);
    render(<AsdrLedger events={events} />);

    const activeRows = screen.queryAllByTestId('active-row');
    expect(activeRows.length).toBe(0);
  });

  it('highlights the closest row to currentTimeSec', () => {
    const events = makeMockEvents(10);
    // 3.4 seconds should be closest to 3 seconds ("T+00:03")
    render(<AsdrLedger events={events} currentTimeSec={3.4} />);

    const activeRow = screen.getByTestId('active-row');
    expect(activeRow).toBeDefined();
    expect(activeRow.textContent).toContain('T+00:03');
    expect(activeRow.className).toContain('highlighted');
    expect(activeRow.style.background).toBe('rgba(91, 192, 190, 0.15)');
  });

  it('transitions pages when closest event is on a different page', () => {
    const events = makeMockEvents(120);
    // Page 1 is 0-49, Page 2 is 50-99
    // 75 seconds -> closest event is index 75 ("T+01:15") which is on page 2 (index 1)
    const { rerender } = render(<AsdrLedger events={events} currentTimeSec={10} />);

    // Initially on page 1
    expect(screen.getByText('1/3')).toBeDefined();
    expect(screen.getByText('T+00:10')).toBeDefined();

    // Rerender with currentTimeSec = 75
    rerender(<AsdrLedger events={events} currentTimeSec={75} />);

    // Should transition to page 2 (1/3 becomes 2/3)
    expect(screen.getByText('2/3')).toBeDefined();

    // Active row on page 2 should be highlighted
    const activeRow = screen.getByTestId('active-row');
    expect(activeRow.textContent).toContain('T+01:15');
  });

  it('calls scrollIntoView when the closest event shifts', () => {
    const events = makeMockEvents(12);
    const { rerender } = render(<AsdrLedger events={events} currentTimeSec={2} />);

    // Initial render scrolls to T+00:02
    expect(scrollMock).toHaveBeenCalledTimes(1);

    // Shift to T+00:08
    rerender(<AsdrLedger events={events} currentTimeSec={8} />);
    expect(scrollMock).toHaveBeenCalledTimes(2);
  });

  it('works correctly when events are filtered', () => {
    const events = makeMockEvents(30);
    // CRIT events are index 0, 10, 20
    render(<AsdrLedger events={events} currentTimeSec={12} />);

    // Filter by CRIT
    const critBtn = screen.getByTestId('asdr-filter-crit');
    fireEvent.click(critBtn);

    // Filter list has 3 CRIT events: index 0 (T+00:00), index 10 (T+00:10), index 20 (T+00:20)
    // 12 is closest to 10 (T+00:10)
    const activeRow = screen.getByTestId('active-row');
    expect(activeRow.textContent).toContain('T+00:10');
  });
});
