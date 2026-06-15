import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { DiagnosticCanvas } from '../DiagnosticCanvas';

describe('DiagnosticCanvas', () => {
  it('shows GO overlay waiting for manual confirmation when verdict is GO', () => {
    render(<DiagnosticCanvas focusedGateId={null} gates={[]} scenarioYaml="" storedYaml="" verdict="GO" countdown={3} transitioning={false} />);
    expect(screen.getByText('所有安全门控通过')).toBeDefined();
    expect(screen.queryByText('3')).toBeNull();
    expect(screen.getByText('请在右下角人工确认 GO 后进入仿真运行')).toBeDefined();
  });

  it('shows GO overlay without auto-launch copy when countdown <= 0', () => {
    render(<DiagnosticCanvas focusedGateId={null} gates={[]} scenarioYaml="" storedYaml="" verdict="GO" countdown={0} transitioning={false} />);
    expect(screen.getByText('所有安全门控通过')).toBeDefined();
    expect(screen.getByText('请在右下角人工确认 GO 后进入仿真运行')).toBeDefined();
  });

  it('shows idle spinner when no focusedGateId', () => {
    render(<DiagnosticCanvas focusedGateId={null} gates={[]} scenarioYaml="" storedYaml="" verdict={null} countdown={-1} transitioning={false} />);
    expect(screen.getByText('Initializing...')).toBeDefined();
  });
  
  it('shows transitioning overlay when transitioning', () => {
    render(<DiagnosticCanvas focusedGateId={null} gates={[]} scenarioYaml="" storedYaml="" verdict="GO" countdown={-1} transitioning={true} />);
    expect(screen.getByText('正在激活 L3 核心...')).toBeDefined();
  });
});
