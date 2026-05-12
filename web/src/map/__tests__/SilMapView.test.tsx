import { describe, it, expect, vi } from 'vitest';
import { render } from '@testing-library/react';
import { SilMapView } from '../SilMapView';

vi.mock('maplibre-gl', () => ({
  default: {
    Map: vi.fn().mockReturnValue({
      addControl: vi.fn(),
      remove: vi.fn(),
    }),
    NavigationControl: vi.fn(),
  },
}));

describe('SilMapView', () => {
  it('renders map container div', () => {
    const { getByTestId } = render(<SilMapView />);
    expect(getByTestId('sil-map-view')).toBeInTheDocument();
  });
});
