import { describe, it, expect, beforeEach } from 'vitest';
import { useUIStore } from '../uiStore';

describe('uiStore', () => {
  beforeEach(() => { useUIStore.getState().reset(); });

  it('viewMode defaults to captain', () => {
    expect(useUIStore.getState().viewMode).toBe('captain');
  });

  it('accepts engineer viewMode', () => {
    useUIStore.getState().setViewMode('engineer');
    expect(useUIStore.getState().viewMode).toBe('engineer');
  });

  it('leftDrawerOpen defaults to false', () => {
    expect(useUIStore.getState().leftDrawerOpen).toBe(false);
  });

  it('toggleLeftDrawer flips leftDrawerOpen', () => {
    useUIStore.getState().toggleLeftDrawer();
    expect(useUIStore.getState().leftDrawerOpen).toBe(true);
    useUIStore.getState().toggleLeftDrawer();
    expect(useUIStore.getState().leftDrawerOpen).toBe(false);
  });

  it('toggleRightDrawer flips rightDrawerOpen', () => {
    useUIStore.getState().toggleRightDrawer();
    expect(useUIStore.getState().rightDrawerOpen).toBe(true);
  });

  it('reset restores all defaults', () => {
    useUIStore.getState().setViewMode('engineer');
    useUIStore.getState().toggleLeftDrawer();
    useUIStore.getState().reset();
    const s = useUIStore.getState();
    expect(s.viewMode).toBe('captain');
    expect(s.leftDrawerOpen).toBe(false);
    expect(s.rightDrawerOpen).toBe(false);
  });
});
