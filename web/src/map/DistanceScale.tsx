import React from 'react';

interface DistanceScaleProps {
  nmPerPixel: number;
}

export const DistanceScale: React.FC<DistanceScaleProps> = React.memo(({ nmPerPixel }) => {
  const targetPx = 100;
  const rawNm = targetPx * nmPerPixel;
  const niceNm = rawNm < 0.5 ? 0.5 : rawNm < 1 ? 1 : rawNm < 2 ? 2 : rawNm < 5 ? 5 : 10;
  const pxWidth = niceNm / nmPerPixel;

  return (
    <div
      data-testid="distance-scale"
      style={{
        display: 'flex', flexDirection: 'column', alignItems: 'center',
        gap: 2,
      }}
    >
      <div style={{ width: pxWidth, height: 2, background: 'var(--txt-2)' }} />
      <div style={{ fontSize: 8, color: 'var(--txt-3)', fontFamily: 'var(--f-mono)' }}>
        {niceNm} nm
      </div>
    </div>
  );
});
DistanceScale.displayName = 'DistanceScale';
