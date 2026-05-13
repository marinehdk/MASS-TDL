import { useEffect } from 'react';
import { useFsmStore, useUIStore } from '../store';

export interface HotkeyHandlers {
  onTor?: () => void;          // T
  onFault?: () => void;        // F
  onMrc?: () => void;          // M
  onHandback?: () => void;     // H
  onSpace?: () => void;        // SPACE
  onArrowLeft?: () => void;    // ← (manual rudder -1°)
  onArrowRight?: () => void;   // → (manual rudder +1°)
  onArrowUp?: () => void;      // ↑ (manual throttle +5%)
  onArrowDown?: () => void;    // ↓ (manual throttle -5%)
}

export function useHotkeys(handlers: HotkeyHandlers) {
  const setViewMode = useUIStore((s) => s.setViewMode);
  const viewMode = useUIStore((s) => s.viewMode);

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      // Skip if focus is on input/textarea
      const ae = document.activeElement;
      if (ae && (ae.tagName === 'INPUT' || ae.tagName === 'TEXTAREA' || ae.getAttribute('contenteditable') === 'true')) {
        return;
      }

      // Cmd/Ctrl + Shift + G: toggle view
      if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key.toLowerCase() === 'g') {
        e.preventDefault();
        setViewMode(viewMode === 'captain' ? 'god' : 'captain');
        return;
      }

      switch (e.key.toLowerCase()) {
        case 't': handlers.onTor?.(); break;
        case 'f': handlers.onFault?.(); break;
        case 'm': handlers.onMrc?.(); break;
        case 'h': handlers.onHandback?.(); break;
        case ' ': handlers.onSpace?.(); e.preventDefault(); break;
        case 'arrowleft':  handlers.onArrowLeft?.(); break;
        case 'arrowright': handlers.onArrowRight?.(); break;
        case 'arrowup':    handlers.onArrowUp?.(); break;
        case 'arrowdown':  handlers.onArrowDown?.(); break;
      }
    };

    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [handlers, viewMode, setViewMode]);
}
