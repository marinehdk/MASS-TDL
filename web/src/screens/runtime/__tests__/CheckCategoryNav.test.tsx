import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { CheckCategoryNav } from '../CheckCategoryNav';

describe('CheckCategoryNav', () => {
  it('shows runtime categories and selects a category', () => {
    const onSelect = vi.fn();

    render(
      <CheckCategoryNav
        selected="mode"
        onSelect={onSelect}
        status={{
          mode: 'ACTIVE',
          core: '4/4',
          plugins: '2/3',
          ros: 'OK',
          safety: 'OK',
          verdict: 'WAIT',
        }}
      />,
    );

    expect(screen.getByText('TDL 核心容器')).toBeInTheDocument();
    expect(screen.getByText('外部插件容器')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: /外部插件容器/i }));
    expect(onSelect).toHaveBeenCalledWith('plugins');
  });
});
