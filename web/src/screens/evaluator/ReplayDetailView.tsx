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
}

const toTimelineEvent = (event: EvidenceReplayEvent): TimelineEvent => ({
  t: event.sim_t,
  k: event.event_type,
  sev: event.severity === 'crit' || event.severity === 'warn' ? event.severity : 'info',
  m: event.module,
  d: event.payload_json,
});

export function ReplayDetailView({ evidenceId, scenarioId }: ReplayDetailViewProps) {
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
      gap: 12,
      padding: 16,
      background: 'var(--bg-0)',
      color: 'var(--txt-1)',
      position: 'relative',
    }}>
      <main style={{ minWidth: 0, display: 'flex', flexDirection: 'column', gap: 12 }}>
        <div style={{
          display: 'flex',
          justifyContent: 'space-between',
          gap: 12,
          color: 'var(--txt-1)',
          fontFamily: 'var(--f-mono)',
          fontSize: 12,
        }}>
          <span>{data.session.session_id}</span>
          <span>{data.scenario.scenario_id}</span>
        </div>
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
