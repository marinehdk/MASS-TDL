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
  activeTab: null as 'environment' | 'vessels' | 'assertions' | 'raw' | null,
  onActiveTabChange: noop as (tab: 'environment' | 'vessels' | 'assertions' | 'raw' | null) => void,
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
    const onTabChange = vi.fn();
    render(<BuilderRightRail {...defaultProps} activeTab={null} onActiveTabChange={onTabChange} />);
    fireEvent.click(screen.getByTitle('船舶与任务'));
    expect(onTabChange).toHaveBeenCalledWith('vessels');
  });

  it('deactivates tab on second click', () => {
    const onTabChange = vi.fn();
    render(<BuilderRightRail {...defaultProps} activeTab="vessels" onActiveTabChange={onTabChange} />);
    fireEvent.click(screen.getByTitle('船舶与任务'));
    expect(onTabChange).toHaveBeenCalledWith(null);
  });

  it('renders Environment tab inputs correctly', () => {
    render(<BuilderRightRail {...defaultProps} activeTab="environment" />);
    expect(screen.getByText('1. 自然环境 (ENVIRONMENT)')).toBeTruthy();
    expect(screen.getByText('2. 传感器干扰 (DISTURBANCE)')).toBeTruthy();
    expect(screen.getByText('3. 故障注入时间轴')).toBeTruthy();
  });

  it('locks inputs in Environment tab when isBaseline is true', () => {
    render(<BuilderRightRail {...defaultProps} activeTab="environment" isBaseline={true} />);
    expect(screen.getByText('当前为 Baseline 场景，参数已锁定只读')).toBeTruthy();
  });

  it('renders Assertions tab correctly', () => {
    render(<BuilderRightRail {...defaultProps} activeTab="assertions" />);
    expect(screen.getByText('1. 安全结果底线 (OUTCOME)')).toBeTruthy();
    expect(screen.getByText('2. 算法白盒过程断言 (ASSERTIONS)')).toBeTruthy();
  });
});
