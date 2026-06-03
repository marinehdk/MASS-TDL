import React from 'react';

interface DecisionTreeProps {
  currentTimeSec?: number;
  ruleChain?: string[];
  events?: Array<{ event_type: string; rule_ref?: string; verdict?: number }>;
}

export const ColregsDecisionTree: React.FC<DecisionTreeProps> = ({
  currentTimeSec = 0,
  ruleChain = [],
  events = [],
}) => {
  const layers = [
    {
      id: 'L1',
      level: 1,
      title: 'L1: ODD Check',
      isActive: currentTimeSec >= 25,
      activeText: 'Target MMSI Detected',
      pendingText: 'Target Detection Pending',
      activeDetail: 'MMSI: 413831200 • Inside Operational Domain',
      pendingDetail: 'Scanning radar & AIS streams...',
      color: 'var(--c-info)',
    },
    {
      id: 'L2',
      level: 2,
      title: 'L2: Encounter Classification',
      isActive: currentTimeSec >= 49,
      activeText: 'Head-on (Rule 14)',
      pendingText: 'Classification Pending',
      activeDetail: 'Rel. Hdg: 178.5° • Range: 2.1 nm',
      pendingDetail: 'Waiting for stable target tracks...',
      color: 'var(--c-warn)',
    },
    {
      id: 'L3',
      level: 3,
      title: 'L3: Responsibility Assignment',
      isActive: currentTimeSec >= 49,
      activeText: 'Give-Way Action Required',
      pendingText: 'Responsibility Evaluation Pending',
      activeDetail: 'Own ship designated as GIVE-WAY (Rule 16)',
      pendingDetail: 'Calculating risk of collision indices...',
      color: 'var(--c-danger)',
    },
    {
      id: 'L4',
      level: 4,
      title: 'L4: Maneuver Determination',
      isActive: currentTimeSec >= 52,
      activeText: 'Starboard Avoidance Turn',
      pendingText: 'Maneuver Selection Pending',
      activeDetail: 'Action: Alter course to Starboard ≥ 30°',
      pendingDetail: 'Awaiting L3 responsibility outcome...',
      color: 'var(--c-stbd)',
    },
    {
      id: 'L5',
      level: 5,
      title: 'L5: Status Execution',
      isActive: currentTimeSec >= 152,
      activeText: 'Avoidance maneuver completed',
      pendingText: 'Maneuver Execution Pending',
      activeDetail: 'Safe clearance achieved • Resuming route',
      pendingDetail: 'Maneuver in progress / tracking trajectory...',
      color: 'var(--c-phos)',
    },
  ];

  return (
    <div data-testid="decision-tree" className="decision-tree-container">
      <style dangerouslySetInnerHTML={{ __html: inlineStyles }} />
      
      <div className="decision-tree-header">
        <span className="decision-tree-title">COLREGs Decision Pipeline</span>
        <span className="decision-tree-time">T: {currentTimeSec}s</span>
      </div>

      <div className="decision-tree-body">
        {layers.map((layer, index) => {
          const hasNext = index < layers.length - 1;
          const nextActive = hasNext && layers[index + 1].isActive;
          const lineActive = layer.isActive;
          const lineActiveToActive = layer.isActive && nextActive;

          return (
            <div
              key={layer.id}
              data-testid={`colregs-layer-${layer.level}`}
              className={`decision-node`}
              style={{
                '--node-color': layer.color,
                color: layer.isActive ? layer.color : 'var(--txt-3)',
              } as React.CSSProperties}
            >
              <div className="decision-node-left">
                <div className="decision-node-dot-container">
                  <div className={`decision-node-pulse-ring ${layer.isActive ? 'active' : ''}`} />
                  <div className={`decision-node-dot-outer ${layer.isActive ? 'active' : ''}`}>
                    <div className={`decision-node-dot-inner ${layer.isActive ? 'active' : ''}`} />
                  </div>
                </div>
                {hasNext && (
                  <div
                    className={`decision-node-line ${lineActive ? 'active' : ''} ${
                      lineActiveToActive ? 'active-to-active' : ''
                    }`}
                  />
                )}
              </div>

              <div className={`decision-card ${layer.isActive ? 'active' : 'pending'}`}>
                <div className="decision-card-header">
                  <span className={`decision-card-level ${layer.isActive ? 'active' : ''}`}>
                    LAYER 0{layer.level}
                  </span>
                  <span className={`decision-card-status-badge ${layer.isActive ? 'active' : 'pending'}`}>
                    {layer.isActive ? 'Active' : 'Pending'}
                  </span>
                </div>
                <div className={`decision-card-title ${layer.isActive ? 'active' : ''}`}>
                  {layer.title}
                </div>
                <div className={`decision-card-text ${layer.isActive ? 'active' : 'pending'}`}>
                  {layer.isActive ? layer.activeText : layer.pendingText}
                </div>
                <div className={`decision-card-details ${layer.isActive ? 'active' : ''}`}>
                  {layer.isActive ? layer.activeDetail : layer.pendingDetail}
                </div>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};

const inlineStyles = `
  .decision-tree-container {
    background: var(--bg-1);
    border: 1px solid var(--line-2);
    padding: 14px;
    font-family: var(--f-body);
    display: flex;
    flex-direction: column;
    height: 100%;
    overflow-y: auto;
  }

  .decision-tree-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid var(--line-2);
    padding-bottom: 6px;
    margin-bottom: 14px;
  }

  .decision-tree-title {
    font-family: var(--f-disp);
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.15em;
    color: var(--c-phos);
    text-transform: uppercase;
  }

  .decision-tree-time {
    font-family: var(--f-mono);
    font-size: 10px;
    color: var(--c-phos);
    background: var(--bg-2);
    padding: 1px 6px;
    border: 1px solid var(--line-2);
  }

  .decision-tree-body {
    display: flex;
    flex-direction: column;
    gap: 12px;
  }

  .decision-node {
    display: flex;
    gap: 12px;
    position: relative;
  }

  .decision-node-left {
    display: flex;
    flex-direction: column;
    align-items: center;
    width: 20px;
    position: relative;
  }

  .decision-node-dot-container {
    position: relative;
    z-index: 2;
    width: 14px;
    height: 14px;
    margin-top: 10px;
    display: flex;
    align-items: center;
    justify-content: center;
  }

  @keyframes pulse-ring {
    0% {
      transform: scale(0.85);
      opacity: 0.6;
    }
    50% {
      transform: scale(1.3);
      opacity: 0.8;
    }
    100% {
      transform: scale(1.8);
      opacity: 0;
    }
  }

  .decision-node-pulse-ring {
    position: absolute;
    width: 20px;
    height: 20px;
    border-radius: 50%;
    background: currentColor;
    opacity: 0;
    z-index: 1;
  }

  .decision-node-pulse-ring.active {
    animation: pulse-ring 2.5s infinite cubic-bezier(0.25, 0, 0, 1);
  }

  .decision-node-dot-outer {
    width: 14px;
    height: 14px;
    border-radius: 50%;
    border: 1.5px solid var(--line-3);
    background: var(--bg-1);
    transition: all 0.25s ease-in-out;
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 2;
  }

  .decision-node-dot-outer.active {
    border-color: currentColor;
    box-shadow: 0 0 6px currentColor;
  }

  .decision-node-dot-inner {
    width: 5px;
    height: 5px;
    border-radius: 50%;
    background: var(--line-3);
    transition: all 0.25s ease-in-out;
  }

  .decision-node-dot-inner.active {
    background: currentColor;
    box-shadow: 0 0 3px currentColor;
  }

  .decision-node-line {
    position: absolute;
    top: 24px;
    bottom: -16px;
    left: 50%;
    width: 1.5px;
    transform: translateX(-50%);
    background: var(--line-1);
    z-index: 1;
    transition: background-color 0.25s ease-in-out;
  }

  .decision-node-line.active {
    background: var(--line-3);
  }

  .decision-node-line.active-to-active {
    background: var(--node-color);
  }

  .decision-card {
    flex: 1;
    background: var(--bg-2);
    border: 1px solid var(--line-2);
    border-left: 3px solid var(--line-2);
    padding: 8px 12px;
    transition: all 0.25s ease-in-out;
    position: relative;
    overflow: hidden;
  }

  .decision-card.active {
    background: var(--bg-3);
    border-color: var(--line-3);
    border-left-color: var(--node-color);
    box-shadow: 0 3px 10px rgba(0, 0, 0, 0.4);
  }

  .decision-card.active:hover {
    transform: translateX(3px);
    border-color: var(--node-color);
    box-shadow: 0 4px 14px rgba(0, 0, 0, 0.5), 0 0 6px var(--node-color);
  }

  .decision-card.pending {
    opacity: 0.45;
  }

  .decision-card.pending:hover {
    opacity: 0.7;
    transform: translateX(1px);
  }

  .decision-card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 2px;
  }

  .decision-card-level {
    font-family: var(--f-mono);
    font-size: 8.5px;
    font-weight: 700;
    color: var(--txt-3);
  }

  .decision-card-level.active {
    color: var(--txt-2);
  }

  .decision-card-status-badge {
    font-family: var(--f-disp);
    font-size: 8px;
    font-weight: 700;
    letter-spacing: 0.05em;
    padding: 0px 4px;
    text-transform: uppercase;
    border: 1.5px solid transparent;
  }

  .decision-card-status-badge.active {
    color: var(--node-color);
    border-color: var(--node-color);
    background: rgba(255, 255, 255, 0.02);
  }

  .decision-card-status-badge.pending {
    color: var(--txt-3);
    border-color: var(--line-1);
  }

  .decision-card-title {
    font-size: 11px;
    font-weight: 700;
    color: var(--txt-2);
    margin-bottom: 1px;
    transition: color 0.25s ease-in-out;
  }

  .decision-card-title.active {
    color: var(--txt-0);
  }

  .decision-card-text {
    font-size: 10px;
    font-weight: 600;
    margin-bottom: 4px;
    transition: color 0.25s ease-in-out;
  }

  .decision-card-text.active {
    color: var(--txt-1);
  }

  .decision-card-text.pending {
    color: var(--txt-3);
  }

  .decision-card-details {
    font-family: var(--f-mono);
    font-size: 9px;
    color: var(--txt-3);
    transition: color 0.25s ease-in-out;
    line-height: 1.3;
  }

  .decision-card-details.active {
    color: var(--txt-2);
  }
`;

