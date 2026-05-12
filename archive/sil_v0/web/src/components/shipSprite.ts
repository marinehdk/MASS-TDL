/** Own-ship sprite: phosphor triangle + heading line, 24x36 viewBox, color #5BC0BE (--color-phos) */
export const OWN_SHIP_SPRITE = (() => {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="36" viewBox="0 0 24 36">
    <polygon points="12,0 4,14 8,16 12,10 16,16 20,14" fill="#5BC0BE" stroke="#070C13" stroke-width="1.5"/>
    <line x1="12" y1="0" x2="12" y2="26" stroke="#5BC0BE" stroke-width="1.2"/>
  </svg>`;
  return 'data:image/svg+xml;base64,' + btoa(svg);
})();

/** Target ship sprite: same triangle shape, 20x30 viewBox. Color applied via MapLibre icon-color paint. */
export const TARGET_SHIP_SPRITE = (() => {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="20" height="30" viewBox="0 0 20 30">
    <polygon points="10,0 3,12 6,13 10,8 14,13 17,12" fill="#F26B6B" stroke="#070C13" stroke-width="1.2"/>
    <line x1="10" y1="0" x2="10" y2="22" stroke="#F26B6B" stroke-width="1"/>
  </svg>`;
  return 'data:image/svg+xml;base64,' + btoa(svg);
})();
