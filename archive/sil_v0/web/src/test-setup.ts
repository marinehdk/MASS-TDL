import '@testing-library/jest-dom';

// Polyfill scrollIntoView for jsdom
if (typeof Element !== 'undefined' && !Element.prototype.scrollIntoView) {
  Element.prototype.scrollIntoView = () => {};
}

// Polyfill URL.createObjectURL for maplibre-gl in jsdom
if (typeof window !== 'undefined' && !window.URL.createObjectURL) {
  window.URL.createObjectURL = () => '';
}
if (typeof window !== 'undefined' && !window.URL.revokeObjectURL) {
  window.URL.revokeObjectURL = () => {};
}
