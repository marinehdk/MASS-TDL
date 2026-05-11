export default function App() {
  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: '1fr 320px',
      height: '100vh',
    }}>
      {/* Chart Area — MapLibre GL (filled by later tasks) */}
      <div style={{
        background: 'var(--bg-0)',
        position: 'relative',
        overflow: 'hidden',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
      }}>
        <span style={{ color: 'var(--txt-2)', fontFamily: 'var(--fnt-mono)', fontSize: '13px' }}>
          MAP AREA — Trondheim Fjord
        </span>
      </div>

      {/* Side Panel — filled by Task 4 */}
      <div style={{
        background: 'var(--bg-1)',
        borderLeft: '1px solid var(--line-1)',
        overflowY: 'auto',
        display: 'flex',
        flexDirection: 'column',
      }}>
        <div style={{
          padding: '18px 20px',
          borderBottom: '1px solid var(--line-1)',
        }}>
          <div style={{
            fontFamily: 'var(--fnt-disp)',
            fontSize: '11px',
            letterSpacing: '0.22em',
            color: 'var(--color-phos)',
            fontWeight: 600,
          }}>
            SIDE PANEL
          </div>
        </div>
      </div>
    </div>
  );
}
