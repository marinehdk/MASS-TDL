import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { BuilderRightRail } from '../BuilderRightRail';

const defaultProps = {
  previewData: null,
  onRun: vi.fn(),
  onSave: vi.fn(),
  onValidate: vi.fn(),
};

describe('BuilderRightRail', () => {
  it('renders collapsed at 48px by default', () => {
    const { container } = render(<BuilderRightRail {...defaultProps} />);
    const rail = container.firstChild as HTMLElement;
    expect(rail).toBeTruthy();
    // Default: no activeTab => collapsed at 48px
    expect(rail.style.width).toBe('48px');
  });

  it('shows 4 tab icons with title attributes', () => {
    render(<BuilderRightRail {...defaultProps} />);
    expect(screen.getByTitle('Encounter')).toBeTruthy();
    expect(screen.getByTitle('Environment')).toBeTruthy();
    expect(screen.getByTitle('Run')).toBeTruthy();
    expect(screen.getByTitle('Summary')).toBeTruthy();
  });

  it('expands to 320px when tab clicked', () => {
    const { container } = render(<BuilderRightRail {...defaultProps} />);
    const encounterBtn = screen.getByTitle('Encounter');
    fireEvent.click(encounterBtn);
    const rail = container.firstChild as HTMLElement;
    expect(rail.style.width).toBe('320px');
  });

  it('shows Encounter fields when Encounter tab active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Encounter'));
    expect(screen.getByText('DCPA Target')).toBeTruthy();
  });

  it('shows Environment fields when Environment tab active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Environment'));
    expect(screen.getByText('Beaufort')).toBeTruthy();
  });

  it('shows Run fields when Run tab active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Run'));
    expect(screen.getByText('Duration')).toBeTruthy();
  });

  it('shows Summary with Run button when Summary tab active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Summary'));
    expect(screen.getByTestId('run-action-btn')).toBeTruthy();
  });

  it('collapses back to 48px when collapse button clicked', () => {
    const { container } = render(<BuilderRightRail {...defaultProps} />);
    // Expand first
    fireEvent.click(screen.getByTitle('Encounter'));
    const rail = container.firstChild as HTMLElement;
    expect(rail.style.width).toBe('320px');
    // Click collapse button
    const collapseBtn = screen.getByTitle('Collapse');
    fireEvent.click(collapseBtn);
    expect(rail.style.width).toBe('48px');
  });

  it('calls onRun when Run button clicked', () => {
    const onRun = vi.fn();
    render(<BuilderRightRail {...defaultProps} onRun={onRun} />);
    fireEvent.click(screen.getByTitle('Summary'));
    const runBtn = screen.getByTestId('run-action-btn');
    fireEvent.click(runBtn);
    expect(onRun).toHaveBeenCalledTimes(1);
  });
});