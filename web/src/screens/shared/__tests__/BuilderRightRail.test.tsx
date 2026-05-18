import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { BuilderRightRail } from '../BuilderRightRail';

const noop = () => {};
const defaultProps = {
  yamlEditor: 'ownship:\n  name: TestVessel',
  onUpdateYaml: noop,
  onChangeRawYaml: noop,
  onRun: noop,
  onSave: noop,
  isBaseline: false,
};

describe('BuilderRightRail', () => {
  it('renders with 400px width', () => {
    const { container } = render(<BuilderRightRail {...defaultProps} />);
    const rail = container.firstChild as HTMLElement;
    expect(rail.style.width).toBe('400px');
  });

  it('shows tab buttons', () => {
    render(<BuilderRightRail {...defaultProps} />);
    expect(screen.getByTitle('船舶与任务')).toBeTruthy();
    expect(screen.getByTitle('环境与故障')).toBeTruthy();
    expect(screen.getByTitle('行为断言')).toBeTruthy();
    expect(screen.getByTitle('源码 (YAML)')).toBeTruthy();
  });

  it('activates tab on click', () => {
    render(<BuilderRightRail {...defaultProps} />);
    fireEvent.click(screen.getByTitle('船舶与任务'));
    expect(screen.getByText('船舶与任务')).toBeTruthy();
  });

  it('deactivates tab on second click', () => {
    render(<BuilderRightRail {...defaultProps} />);
    const tab = screen.getByTitle('船舶与任务');
    fireEvent.click(tab);
    fireEvent.click(tab);
    expect(screen.queryByText('本船配置')).toBeNull();
  });
});
