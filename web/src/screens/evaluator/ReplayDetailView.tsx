import { useMemo, useState } from 'react';
import {
  useGetEvidenceReplayQuery,
  type EvidenceReplayEvent,
} from '../../api/silApi';
import { TimelineSixLane, type TimelineEvent } from '../shared/TimelineSixLane';
import { TrajectoryReplay } from '../shared/TrajectoryReplay';
import { ChainInspector } from './ChainInspector';

interface ReplayDetailViewProps {
  evidenceId: string;
  scenarioId: string;
  scenarioIds?: string[];
  onScenarioChange?: (scenarioId: string) => void;
  onBack?: () => void;
}

const toTimelineEvent = (event: EvidenceReplayEvent): TimelineEvent => ({
  t: event.sim_t,
  k: event.event_type,
  sev: event.severity === 'crit' || event.severity === 'warn' ? event.severity : 'info',
  m: event.module,
  d: event.payload_json,
});

const displayDateTime = (value?: string | null) =>
  value ? value.replace('T', ' ').replace(/([+-]\d\d:\d\d|Z)$/, '') : '-';

export function ReplayDetailView({ evidenceId, scenarioId, scenarioIds = [scenarioId], onScenarioChange, onBack }: ReplayDetailViewProps) {
  const [currentTimeSec, setCurrentTimeSec] = useState(0);
  const [selectedEvent, setSelectedEvent] = useState<TimelineEvent | null>(null);
  const [inspectorTimeSec, setInspectorTimeSec] = useState<number | null>(null);
  const { data, isLoading } = useGetEvidenceReplayQuery({ evidenceId, scenarioId });
  const timelineEvents = useMemo(() => (data?.events ?? []).map(toTimelineEvent), [data?.events]);
  const durationSec = Math.max(0, data?.duration_s ?? 0);

  if (isLoading || !data) {
    return <div style={{ padding: 16 }}>Loading replay</div>;
  }

  return (
    <div style={{
      height: '100%',
      display: 'grid',
      gridTemplateColumns: 'minmax(0, 1fr) 360px',
      gridTemplateRows: '34px minmax(0, 1fr)',
      gap: 12,
      padding: 16,
      background: 'var(--bg-0)',
      color: 'var(--txt-1)',
      position: 'relative',
      boxSizing: 'border-box',
    }}>
      <div style={{
        gridColumn: '1 / -1',
        display: 'grid',
        gridTemplateColumns: '160px minmax(0, 1fr) minmax(240px, auto)',
        alignItems: 'center',
        gap: 12,
        color: 'var(--txt-1)',
        fontFamily: 'var(--f-mono)',
        fontSize: 12,
      }}>
        <button
          type="button"
          onClick={onBack}
          aria-label="返回仿真数据库"
          style={{
            width: 112,
            height: 30,
            border: '1px solid var(--line-1)',
            borderRadius: 4,
            background: 'rgba(91,192,190,0.15)',
            color: 'var(--c-phos)',
            fontFamily: 'var(--f-disp)',
            fontSize: 11,
            fontWeight: 700,
            letterSpacing: '0.05em',
            cursor: 'pointer',
            transition: 'all 0.2s',
          }}
        >
          返回
        </button>
        <div style={{
          display: 'flex',
          justifyContent: 'center',
          alignItems: 'center',
          gap: 36,
          minWidth: 0,
        }}>
          <span>会话: {data.session.session_id}</span>
          <span>创建时间: {displayDateTime(data.session.created_at)}</span>
        </div>
        <div style={{ display: 'flex', justifyContent: 'flex-end', minWidth: 0 }}>
          {scenarioIds.length > 1 ? (
            <span style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              想定:
              <select
                value={data.scenario.scenario_id}
                onChange={(event) => onScenarioChange?.(event.target.value)}
                style={{
                  border: '1px solid var(--line-2)',
                  background: 'var(--bg-1)',
                  color: 'var(--txt-1)',
                  fontFamily: 'var(--f-mono)',
                  fontSize: 12,
                  height: 24,
                }}
                aria-label="选择想定"
              >
                {scenarioIds.map((id) => <option key={id} value={id}>{id}</option>)}
              </select>
            </span>
          ) : (
            <span>想定: {data.scenario.scenario_id}</span>
          )}
        </div>
      </div>
      <main style={{ minWidth: 0, display: 'flex', flexDirection: 'column', gap: 12 }}>
        <div className="glass-panel" style={{ flex: 1, minHeight: 0, overflow: 'hidden' }}>
          <TrajectoryReplay
            durationSec={durationSec}
            currentTimeSec={currentTimeSec}
            onTimeChange={setCurrentTimeSec}
            points={data.trajectory}
          />
        </div>
        <div className="glass-panel" style={{ height: 180, overflow: 'hidden' }}>
          <TimelineSixLane
            events={timelineEvents}
            durationSec={durationSec}
            currentTimeSec={currentTimeSec}
            onScrub={setCurrentTimeSec}
            onEventSelect={(event) => {
              setSelectedEvent(event);
              setInspectorTimeSec(event.t);
            }}
          />
        </div>
      </main>
      <aside style={{ display: 'flex', flexDirection: 'column', gap: 8, minWidth: 0 }}>
        <section style={{ border: '1px solid var(--line-2)', background: 'var(--bg-1)', padding: 10 }}>
          <div style={{ fontFamily: 'var(--f-disp)', fontSize: 10, color: 'var(--txt-3)', textTransform: 'uppercase' }}>
            Chain Inspector
          </div>
          <div style={{ fontFamily: 'var(--f-mono)', fontSize: 12, marginTop: 6 }}>
            {inspectorTimeSec != null ? `T+${inspectorTimeSec.toFixed(1)} s` : 'Select a gate or timeline event'}
          </div>
        </section>
        {selectedEvent && (
          <section style={{ border: '1px solid var(--line-2)', background: 'var(--bg-1)', padding: 10 }}>
            <div style={{ fontFamily: 'var(--f-disp)', fontSize: 10, color: 'var(--txt-3)', textTransform: 'uppercase' }}>
              Selected Event
            </div>
            <div style={{ fontFamily: 'var(--f-mono)', fontSize: 12, marginTop: 6 }}>
              {selectedEvent.m} {selectedEvent.k}
            </div>
          </section>
        )}
        {data.gates.map((gate) => (
          <button
            key={`${gate.gate_id}-${gate.source}`}
            type="button"
            onClick={() => setInspectorTimeSec(currentTimeSec)}
            style={{
              textAlign: 'left',
              border: '1px solid var(--line-2)',
              background: 'var(--bg-1)',
              color: 'var(--txt-1)',
              padding: 8,
              cursor: 'pointer',
            }}
          >
            <strong>{gate.gate_id}</strong> {gate.status}
          </button>
        ))}
      </aside>
      {inspectorTimeSec != null && (
        <ChainInspector
          evidenceId={evidenceId}
          scenarioId={scenarioId}
          simT={inspectorTimeSec}
          onClose={() => setInspectorTimeSec(null)}
        />
      )}
    </div>
  );
}
