import { useRef, useCallback } from 'react';

/** Throttle callback to maxFps. Drops calls within the min interval. */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export function useThrottledTopic<T extends (...args: any[]) => void>(
  callback: T,
  maxFps: number = 30,
): T {
  const lastUpdate = useRef(0);
  const minInterval = 1000 / maxFps;

  return useCallback(
    ((...args: any[]) => {
      const now = performance.now();
      if (now - lastUpdate.current >= minInterval) {
        lastUpdate.current = now;
        callback(...args);
      }
    }) as T,
    [callback, minInterval],
  );
}
