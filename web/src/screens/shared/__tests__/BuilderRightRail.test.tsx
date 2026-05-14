import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { BuilderRightRail } from '../BuilderRightRail';

const noop = () => {};
const defaultProps = {
  previewData: null,
  onRun: noop,
  onSave: noop,
  onValidate: noop,
};

describe('BuilderRightRail', () => {
  it('renders collapsed at 48px by default', () => {
    const { container } = render(<BuilderRightRail {...defaultProps} />);
    const rail = container.firstChild as HTMLElement;
    expect(rail.style.width).toBe('48px');
  });

  it('shows 4 tab icons in collapsed state', () => {
    render(<BuilderRightRail {...defaultProps} />);
    expect(screen.getByTitle('Encounter')).toBeTruthy();
    expect(screen.getByTitle('Environment')).toBeTruthy();
    expect(screen.getByTitle('Run')).toBeTruthy();
    expect(screen.getByTitle('Summary')).toBeTruthy();
  });

  it('expands to 320px when tab is clicked', () => {
    const { container } = render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Encounter'));
    const rail = container.firstChild as HTMLElement;
    expect(rail.style.width).toBe('320px');
  });

  it('shows Encounter fields when Encounter tab is active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Encounter'));
    expect(screen.getByText(/DCPA Target/i)).toBeTruthy();
  });

  it('shows Environment fields when Environment tab is active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Environment'));
    expect(screen.getByText(/Beaufort/i)).toBeTruthy();
  });

  it('shows Run fields when Run tab is active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Run'));
    expect(screen.getByText(/Duration/i)).toBeTruthy();
  });

  it('shows Summary with Run button when Summary tab is active', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Summary'));
    expect(screen.getByText(/RUN/i)).toBeTruthy();
  });

  it('collapses when collapse button is clicked', () => {
    const { container } = render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('Encounter'));
    fireEvent.click(screen.getByTitle('Collapse panel'));
    const rail = container.firstChild as HTMLElement;
    expect(rail.style.width).toBe('48px');
  });

  it('calls onRun when Run button is clicked', () => {
    const onRun = vi.fn();
    render(<BuilderRightRail {...defaultProps} onRun={onRun} />);
    fireEvent.click(screen.getByTitle('Summary'));
    fireEvent.click(screen.getByText(/RUN/i));
    expect(onRun).toHaveBeenCalledOnce();
  });
});