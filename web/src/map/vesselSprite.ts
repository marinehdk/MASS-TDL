export function createOwnShipSVG(headingDeg: number, color = '#00ff88'): string {
  return `<svg width="40" height="60" viewBox="0 0 40 60" xmlns="http://www.w3.org/2000/svg">
    <polygon points="20,0 0,50 20,40 40,50" fill="${color}" stroke="#ffffff" stroke-width="1"
             transform="rotate(${headingDeg}, 20, 30)"/>
  </svg>`;
}

export function createTargetSVG(headingDeg: number, color = '#ff4444'): string {
  return `<svg width="30" height="45" viewBox="0 0 30 45" xmlns="http://www.w3.org/2000/svg">
    <circle cx="15" cy="15" r="14" fill="none" stroke="${color}" stroke-width="2"
            transform="rotate(${headingDeg}, 15, 22)"/>
    <line x1="15" y1="0" x2="15" y2="30" stroke="${color}" stroke-width="2"
          transform="rotate(${headingDeg}, 15, 22)"/>
  </svg>`;
}
