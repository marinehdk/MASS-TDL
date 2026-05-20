import { useState, useMemo, useEffect } from 'react';
import * as jsyaml from 'js-yaml';
import Editor from '@monaco-editor/react';
import { useSchemaValidation } from '../../hooks/useSchemaValidation';
import { useScenarioStore } from '../../store';
import { 
  LucideShip, LucideCloudRain, 
  LucideFileJson, LucideSave, LucideChevronRight,
  LucideLayout, LucideRotateCcw
} from 'lucide-react';

interface FieldProps {
  label: string;
  value: string | number;
  onChange: (val: string) => void;
  type?: string;
  unit?: string;
  description?: string;
}

function Field({ label, value, onChange, type = 'text', unit = '', description }: FieldProps) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 6, marginBottom: 16 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <label style={{ fontSize: 13, fontWeight: 600, fontFamily: 'var(--f-mono)', color: 'var(--txt-2)' }}>
          {label}
        </label>
        {unit && <span style={{ fontSize: 12, color: 'var(--txt-3)', fontFamily: 'var(--f-mono)' }}>{unit}</span>}
      </div>
      <input 
        type={type} 
        value={value ?? ''} 
        onChange={(e) => onChange(e.target.value)}
        style={{
          background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
          color: 'var(--txt-1)', padding: '8px 10px', borderRadius: 6,
          fontFamily: 'var(--f-mono)', fontSize: 14, outline: 'none', width: '100%',
          transition: 'border-color 0.2s'
        }}
        onFocus={(e) => e.target.style.borderColor = 'var(--c-phos)'}
        onBlur={(e) => e.target.style.borderColor = 'var(--line-1)'}
      />
      {description && <div style={{ fontSize: 12, color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', marginTop: 2 }}>{description}</div>}
    </div>
  );
}

function Select({ 
  label, 
  value, 
  onChange, 
  options,
  suffix 
}: { 
  label: string; 
  value: string; 
  onChange: (val: string) => void; 
  options: string[] | Array<{ value: string; label: string }>;
  suffix?: string;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 6, marginBottom: 16 }}>
      <label style={{ fontSize: 13, fontWeight: 600, fontFamily: 'var(--f-mono)', color: 'var(--txt-2)' }}>{label}</label>
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        <select 
          value={value} 
          onChange={(e) => onChange(e.target.value)}
          style={{
            flex: 1,
            background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
            color: 'var(--txt-1)', padding: '8px 10px', borderRadius: 6,
            fontFamily: 'var(--f-mono)', fontSize: 14, outline: 'none',
            cursor: 'pointer'
          }}
        >
          {options.map((o) => {
            const val = typeof o === 'string' ? o : o.value;
            const lbl = typeof o === 'string' ? o : o.label;
            return <option key={val} value={val}>{lbl}</option>;
          })}
        </select>
        {suffix && (
          <div style={{
            background: 'rgba(91,192,190,0.12)',
            border: '1px solid rgba(91,192,190,0.3)',
            borderRadius: 6,
            padding: '8px 12px',
            color: 'var(--c-phos)',
            fontFamily: 'var(--f-mono)',
            fontSize: 13,
            whiteSpace: 'nowrap'
          }}>
            {suffix}
          </div>
        )}
      </div>
    </div>
  );
}

function SectionTitle({ title, children }: { title: string; children?: React.ReactNode }) {
  return (
    <div style={{ 
      fontSize: 15, fontWeight: 700, color: 'var(--c-phos)', 
      fontFamily: 'var(--f-disp)', letterSpacing: '0.1em', 
      marginTop: 20, marginBottom: 14, display: 'flex', alignItems: 'center', justifyContent: 'space-between'
    }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <div style={{ width: 5, height: 15, background: 'var(--c-phos)', borderRadius: 2 }} />
        {title.toUpperCase()}
      </div>
      {children}
    </div>
  );
}

export function CollapsibleSection({
  title,
  children,
  extra,
  defaultExpanded = true
}: {
  title: string;
  children: React.ReactNode;
  extra?: React.ReactNode;
  defaultExpanded?: boolean;
}) {
  const [isExpanded, setIsExpanded] = useState(defaultExpanded);
  return (
    <div style={{
      marginBottom: 16,
      background: 'rgba(16, 27, 44, 0.4)',
      border: '1px solid var(--line-1)',
      borderRadius: 8,
      overflow: 'hidden'
    }}>
      <div
        onClick={() => setIsExpanded(!isExpanded)}
        style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          padding: '12px 16px',
          background: isExpanded ? 'rgba(91,192,190,0.08)' : 'transparent',
          cursor: 'pointer',
          borderBottom: isExpanded ? '1px solid var(--line-1)' : 'none',
          transition: 'all 0.2s'
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <div style={{ width: 5, height: 15, background: 'var(--c-phos)', borderRadius: 2 }} />
          <span style={{
            fontSize: 14, fontWeight: 700, color: 'var(--txt-1)',
            fontFamily: 'var(--f-disp)', letterSpacing: '0.1em'
          }}>
            {title.toUpperCase()}
          </span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
          {extra && <div onClick={(e) => e.stopPropagation()}>{extra}</div>}
          <span style={{
            color: 'var(--txt-3)',
            transform: isExpanded ? 'rotate(90deg)' : 'none',
            transition: 'transform 0.2s',
            fontSize: 10
          }}>
            ▶
          </span>
        </div>
      </div>
      {isExpanded && (
        <div style={{ padding: 16 }}>
          {children}
        </div>
      )}
    </div>
  );
}

function VoyageConfigTab({ doc, onUpdate }: { doc: any; onUpdate: (updates: any) => void }) {
  const voyageTask = doc?.voyageTask || {};
  const destination = voyageTask?.destination || {};

  return (
    <CollapsibleSection title="航行任务" defaultExpanded={true}>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <Field 
          label="终点纬度" 
          value={destination?.latitude ?? destination?.lat ?? ''} 
          onChange={(v) => onUpdate({ 'voyageTask.destination.latitude': Number(v) })} 
          unit="LAT"
        />
        <Field 
          label="终点经度" 
          value={destination?.longitude ?? destination?.lon ?? ''} 
          onChange={(v) => onUpdate({ 'voyageTask.destination.longitude': Number(v) })} 
          unit="LON"
        />
      </div>
      <Field 
        label="抵达时间窗 (ETA)" 
        value={voyageTask?.eta ?? ''} 
        onChange={(v) => onUpdate({ 'voyageTask.eta': v })} 
      />
      <Select 
        label="绕航优化偏好" 
        value={voyageTask?.optimization_preference ?? '安全优先'} 
        onChange={(v) => onUpdate({ 'voyageTask.optimization_preference': v })}
        options={['安全优先', '经济优先', '时间优先', '航线长度优先']}
      />
    </CollapsibleSection>
  );
}

function OwnShipConfigTab({ doc, onUpdate }: { doc: any; onUpdate: (updates: any) => void }) {
  const ownShip = doc?.ownShip || {};
  const pos = ownShip?.initial?.position || {};
  const initial = ownShip?.initial || {};

  return (
    <CollapsibleSection title="初始状态设置" defaultExpanded={true}>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <Field 
          label="初始纬度" 
          value={pos?.latitude ?? ''} 
          onChange={(v) => onUpdate({ 'ownShip.initial.position.latitude': Number(v) })} 
          unit="LAT"
        />
        <Field 
          label="初始经度" 
          value={pos?.longitude ?? ''} 
          onChange={(v) => onUpdate({ 'ownShip.initial.position.longitude': Number(v) })} 
          unit="LON"
        />
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <Field 
          label="初始航向" 
          value={initial?.heading ?? pos?.heading ?? ''} 
          onChange={(v) => onUpdate({ 'ownShip.initial.heading': Number(v) })} 
          unit="°"
        />
        <Field 
          label="初始航速(SOG)" 
          value={initial?.sog ?? pos?.speed ?? ''} 
          onChange={(v) => onUpdate({ 'ownShip.initial.sog': Number(v) })} 
          unit="kn"
        />
      </div>
    </CollapsibleSection>
  );
}

function TargetsConfigTab({ doc, onUpdate }: { doc: any; onUpdate: (updates: any) => void }) {
  const targets = doc?.targetShips || doc?.targets || [];
  const [schemaMode, setSchemaMode] = useState<'latlon' | 'enu'>('latlon');
  
  // Accordion active index (default to first target expanded if targets exist)
  const [expandedIdx, setExpandedIdx] = useState<number | null>(targets.length > 0 ? 0 : null);

  const handleAddTarget = () => {
    // Determine new target position offset from ownShip if available
    let lat = 63.44;
    let lon = 10.38;
    if (doc?.ownShip?.initial?.position) {
      lat = Number((doc.ownShip.initial.position.latitude + 0.005).toFixed(6));
      lon = Number((doc.ownShip.initial.position.longitude + 0.005).toFixed(6));
    }

    const newTarget = {
      id: `ts${targets.length + 1}`,
      static: { 
        id: targets.length + 2,
        mmsi: 200000000 + targets.length + 1
      },
      initial: {
        position: { latitude: lat, longitude: lon },
        cog: 0.0,
        sog: 8.0,
        heading: 0.0
      },
      model: 'ais_replay_vessel'
    };
    
    const updated = [...targets, newTarget];
    onUpdate({ 'targetShips': updated });
    setExpandedIdx(targets.length); // Expand the newly added target
  };

  const handleRemoveTarget = (index: number, e: any) => {
    e.stopPropagation(); // Prevent accordion toggling when clicking delete
    const updated = targets.filter((_: any, i: number) => i !== index);
    onUpdate({ 'targetShips': updated });
    
    if (updated.length === 0) {
      setExpandedIdx(null);
    } else if (expandedIdx === index) {
      setExpandedIdx(0);
    } else if (expandedIdx !== null && expandedIdx > index) {
      setExpandedIdx(expandedIdx - 1);
    }
  };

  const addBtn = (
    <button 
      onClick={handleAddTarget}
      style={{
        padding: '4px 10px',
        borderRadius: 6,
        background: 'rgba(91,192,190,0.08)',
        border: '1px solid var(--c-phos)',
        color: 'var(--c-phos)',
        fontFamily: 'var(--f-disp)',
        fontSize: 12,
        fontWeight: 700,
        cursor: 'pointer',
        transition: 'all 0.2s',
        textTransform: 'none'
      }}
      onMouseEnter={(e) => e.currentTarget.style.background = 'rgba(91,192,190,0.18)'}
      onMouseLeave={(e) => e.currentTarget.style.background = 'rgba(91,192,190,0.08)'}
    >
      添加船只
    </button>
  );

  return (
    <CollapsibleSection title="目标船列表" extra={addBtn} defaultExpanded={true}>
      {targets.length === 0 && (
        <div style={{ fontFamily: 'var(--f-mono)', color: 'var(--txt-3)', padding: '20px 20px', textAlign: 'center', fontSize: 13 }}>
          当前场景无目标船数据
        </div>
      )}
      
      {targets.map((tgt: any, idx: number) => {
        const pos = tgt?.initial?.position || {};
        const prefix = `targetShips.${idx}`;
        const isExpanded = expandedIdx === idx;

        return (
          <div key={idx} style={{ 
            marginBottom: 16, 
            background: 'rgba(16, 27, 44, 0.4)',
            border: '1px solid var(--line-1)',
            borderRadius: 8,
            overflow: 'hidden'
          }}>
            <div 
              onClick={() => setExpandedIdx(isExpanded ? null : idx)}
              style={{ 
                display: 'flex', 
                alignItems: 'center', 
                justifyContent: 'space-between',
                padding: '12px 16px', 
                background: isExpanded ? 'rgba(91,192,190,0.08)' : 'transparent',
                cursor: 'pointer',
                borderBottom: isExpanded ? '1px solid var(--line-1)' : 'none',
                transition: 'all 0.2s'
              }}
            >
              <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                <span style={{ 
                  background: 'rgba(91,192,190,0.12)', color: 'var(--c-phos)', border: '1px solid var(--c-phos)', padding: '2px 8px', 
                  borderRadius: 4, fontSize: 11, fontWeight: 700, fontFamily: 'var(--f-disp)' 
                }}>
                  目标船只 #{idx + 1}
                </span>
                <span style={{ fontSize: 14, fontWeight: 700, color: 'var(--txt-1)', fontFamily: 'var(--f-mono)' }}>
                  {tgt.id || 'Unknown'}
                </span>
              </div>
              <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                <button 
                  onClick={(e) => handleRemoveTarget(idx, e)}
                  style={{
                    padding: '4px 10px',
                    borderRadius: 6,
                    background: 'rgba(255, 95, 95, 0.08)',
                    border: '1px solid #ff5f5f',
                    color: '#ff5f5f',
                    fontFamily: 'var(--f-disp)',
                    fontSize: 11,
                    fontWeight: 700,
                    cursor: 'pointer',
                    transition: 'all 0.2s',
                    textTransform: 'none'
                  }}
                  onMouseEnter={(e) => e.currentTarget.style.background = 'rgba(255, 95, 95, 0.18)'}
                  onMouseLeave={(e) => e.currentTarget.style.background = 'rgba(255, 95, 95, 0.08)'}
                >
                  删除船只
                </button>
                <span style={{ 
                  color: 'var(--txt-3)', 
                  transform: isExpanded ? 'rotate(90deg)' : 'none',
                  transition: 'transform 0.2s',
                  fontSize: 10
                }}>
                  ▶
                </span>
              </div>
            </div>

            {isExpanded && (
              <div style={{ padding: 16 }}>
                <Select 
                  label="驱动模型" 
                  value={tgt.model || 'ais_replay_vessel'} 
                  onChange={(v) => onUpdate({ [`${prefix}.model`]: v })}
                  options={[
                    { value: 'ais_replay_vessel', label: 'AIS轨迹回放' },
                    { value: 'fcb_mmg_vessel', label: 'FCB动力学模型' },
                    { value: 'asd_tug_vessel', label: 'ASD拖轮模型' },
                    { value: 'constant_velocity_vessel', label: '恒速运动模型' }
                  ]}
                  suffix={
                    (tgt.model || 'ais_replay_vessel') === 'ais_replay_vessel' ? 'AIS' :
                    tgt.model === 'fcb_mmg_vessel' ? 'MMG' :
                    tgt.model === 'asd_tug_vessel' ? 'Tug' :
                    tgt.model === 'constant_velocity_vessel' ? 'Const' : 'AIS'
                  }
                />
                
                <Select 
                  label="坐标系模式" 
                  value={tgt.initial?.x_m !== undefined ? 'enu' : 'latlon'} 
                  onChange={(v) => {
                    if (v === 'enu') {
                      onUpdate({
                        [`${prefix}.initial.x_m`]: 0.0,
                        [`${prefix}.initial.y_m`]: 0.0,
                        [`${prefix}.initial.position`]: null
                      });
                    } else {
                      let oLat = 63.44;
                      let oLon = 10.38;
                      if (doc?.ownShip?.initial?.position) {
                        oLat = doc.ownShip.initial.position.latitude;
                        oLon = doc.ownShip.initial.position.longitude;
                      }
                      onUpdate({
                        [`${prefix}.initial.position`]: { latitude: oLat, longitude: oLon },
                        [`${prefix}.initial.x_m`]: null,
                        [`${prefix}.initial.y_m`]: null
                      });
                    }
                  }}
                  options={['latlon', 'enu']}
                />

                {tgt.initial?.x_m === undefined ? (
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
                  <Field 
                    label="初始纬度" 
                    value={pos?.latitude ?? ''} 
                    onChange={(v) => onUpdate({ [`${prefix}.initial.position.latitude`]: Number(v) })} 
                    unit="LAT"
                  />
                  <Field 
                    label="初始经度" 
                    value={pos?.longitude ?? ''} 
                    onChange={(v) => onUpdate({ [`${prefix}.initial.position.longitude`]: Number(v) })} 
                    unit="LON"
                  />
                </div>
                ) : (
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
                  <Field 
                    label="X 坐标 (m)" 
                    value={tgt?.initial?.x_m ?? ''} 
                    onChange={(v) => onUpdate({ [`${prefix}.initial.x_m`]: Number(v) })} 
                    unit="m"
                  />
                  <Field 
                    label="Y 坐标 (m)" 
                    value={tgt?.initial?.y_m ?? ''} 
                    onChange={(v) => onUpdate({ [`${prefix}.initial.y_m`]: Number(v) })} 
                    unit="m"
                  />
                </div>
                )}
              </div>
            )}
          </div>
        );
      })}
    </CollapsibleSection>
  );
}

const TABS = [
  { id: 'vessels',     label: '船舶与任务',  icon: <LucideShip size={24} /> },
  { id: 'environment', label: '环境与故障',  icon: <LucideCloudRain size={24} /> },
  { id: 'assertions',  label: '行为断言',    icon: <LucideLayout size={24} /> },
  { id: 'raw',         label: '源码 (YAML)', icon: <LucideFileJson size={24} /> },
] as const;

type TabId = typeof TABS[number]['id'];

export interface BuilderRightRailProps {
  yamlEditor: string;
  onUpdateYaml: (updates: any) => void;
  onChangeRawYaml: (val: string) => void;
  onRun: () => void;
  onSave: () => void;
  onReset?: () => void;
  isBaseline?: boolean;
  scenarioHash?: string;
  activeTab: TabId | null;
  onActiveTabChange: (tab: TabId | null) => void;
}

export function BuilderRightRail({ yamlEditor, onUpdateYaml, onChangeRawYaml, onRun, onSave, onReset, isBaseline, scenarioHash, activeTab, onActiveTabChange }: BuilderRightRailProps) {
  const validation = useSchemaValidation(yamlEditor);

  // Sync validation status to the global scenario store
  useEffect(() => {
    useScenarioStore.getState().setYamlValidation(
      validation.valid,
      validation.errors[0] || null
    );
  }, [validation.valid, validation.errors]);

  const doc = useMemo(() => {
    try {
      return (jsyaml.load(yamlEditor) as any) || {};
    } catch (e) {
      return {};
    }
  }, [yamlEditor]);

  return (
    <>
      {/* TIER 1: Content Panel (Independent Floating Module) */}
      <div style={{
        position: 'absolute', top: 20, right: 100, 
        width: '400px', 
        maxHeight: 'calc(100% - 40px)',
        height: 'auto',
        background: 'rgba(13, 19, 31, 0.95)', 
        backdropFilter: 'blur(16px)',
        border: '1px solid var(--line-2)',
        borderRadius: 12,
        display: 'flex', flexDirection: 'column',
        transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
        opacity: activeTab ? 1 : 0,
        transform: activeTab ? 'translateX(0)' : 'translateX(20px)',
        pointerEvents: activeTab ? 'auto' : 'none',
        zIndex: 105,
        boxShadow: activeTab ? '-20px 0 50px rgba(0,0,0,0.5)' : 'none',
        overflow: 'hidden'
      }}>
        {activeTab && (
          <div style={{ display: 'flex', flexDirection: 'column', maxHeight: '100%', overflow: 'hidden', minHeight: 0 }}>
            <div style={{
              padding: '24px 20px 16px', borderBottom: '1px solid var(--line-1)',
              display: 'flex', justifyContent: 'center', alignItems: 'center',
              flexShrink: 0
            }}>
              <span style={{
                fontFamily: 'var(--f-disp)', fontSize: 15, fontWeight: 700,
                color: 'var(--txt-1)', letterSpacing: '0.2em'
              }}>
                {TABS.find(t => t.id === activeTab)?.label}
              </span>
            </div>

            <div style={{ flex: 1, overflowY: 'auto', padding: '0 20px 40px', minHeight: 0 }}>
              {activeTab === 'vessels' && (
                <div>
                  <VoyageConfigTab doc={doc} onUpdate={onUpdateYaml} />
                  <OwnShipConfigTab doc={doc} onUpdate={onUpdateYaml} />
                  <TargetsConfigTab doc={doc} onUpdate={onUpdateYaml} />
                </div>
              )}
              {activeTab === 'environment' && (
                <div style={{ padding: '20px 0' }}>
                  <div style={{
                    textAlign: 'center', padding: '40px 20px',
                    background: 'rgba(240,183,47,0.05)', borderRadius: 8,
                    border: '1px solid rgba(240,183,47,0.15)'
                  }}>
                    <div style={{ fontSize: 32, marginBottom: 16, opacity: 0.3 }}>🔒</div>
                    <div style={{ fontFamily: 'var(--f-mono)', fontSize: 14, fontWeight: 600, color: 'var(--txt-1)', marginBottom: 8 }}>
                      Phase 2 功能 (D2.x)
                    </div>
                    <div style={{ fontFamily: 'var(--f-mono)', fontSize: 13, color: 'var(--txt-3)', lineHeight: 1.6 }}>
                      当前版本: 仅展示 metadata.environment 字段摘要
                    </div>
                  </div>
                  {doc?.environment && (
                    <div style={{ marginTop: 20, padding: '12px 16px', background: 'rgba(0,0,0,0.15)', borderRadius: 6, border: '1px solid var(--line-1)' }}>
                      <div style={{ fontFamily: 'var(--f-mono)', fontSize: 14, color: 'var(--txt-1)', lineHeight: 1.8 }}>
                        <div>风速: {doc.environment?.wind?.speed_mps ?? '—'} m/s @ {doc.environment?.wind?.dir_deg ?? '—'}°</div>
                        <div>海流: {doc.environment?.current?.speed_mps ?? '—'} m/s @ {doc.environment?.current?.dir_deg ?? '—'}°</div>
                        <div>能见度: {doc.environment?.visibility_nm ?? '—'} nm</div>
                      </div>
                    </div>
                  )}
                </div>
              )}
              {activeTab === 'assertions' && (
                <div style={{ padding: '20px 0' }}>
                  <div style={{
                    textAlign: 'center', padding: '40px 20px',
                    background: 'rgba(240,183,47,0.05)', borderRadius: 8,
                    border: '1px solid rgba(240,183,47,0.15)'
                  }}>
                    <div style={{ fontSize: 32, marginBottom: 16, opacity: 0.3 }}>🔒</div>
                    <div style={{ fontFamily: 'var(--f-mono)', fontSize: 14, fontWeight: 600, color: 'var(--txt-1)', marginBottom: 8 }}>
                      Phase 2 功能 (D2.x)
                    </div>
                    <div style={{ fontFamily: 'var(--f-mono)', fontSize: 13, color: 'var(--txt-3)', lineHeight: 1.6 }}>
                      当前版本: 仅展示 expected_outcome 字段摘要
                    </div>
                  </div>
                  {doc?.metadata?.expected_outcome && (
                    <div style={{ marginTop: 20, padding: '12px 16px', background: 'rgba(0,0,0,0.15)', borderRadius: 6, border: '1px solid var(--line-1)' }}>
                      <div style={{ fontFamily: 'var(--f-mono)', fontSize: 14, color: 'var(--txt-1)', lineHeight: 1.8 }}>
                        {doc.metadata.expected_outcome.cpa_min_m_ge != null && <div>CPA min ≥ {doc.metadata.expected_outcome.cpa_min_m_ge}m</div>}
                        {doc.metadata.expected_outcome.colregs_rules && <div>COLREGs: [{Array.isArray(doc.metadata.expected_outcome.colregs_rules) ? doc.metadata.expected_outcome.colregs_rules.join(', ') : doc.metadata.expected_outcome.colregs_rules}]</div>}
                        {doc.metadata.expected_outcome.grounding && <div>Grounding: {doc.metadata.expected_outcome.grounding}</div>}
                      </div>
                    </div>
                  )}
                </div>
              )}
              {activeTab === 'raw' && (
                <div style={{ flex: 1, height: '400px', margin: '0 -20px' }}>
                  <Editor
                    height="100%"
                    language="yaml"
                    theme="vs-dark"
                    value={yamlEditor}
                    onChange={(value) => onChangeRawYaml(value ?? '')}
                    options={{
                      minimap: { enabled: false },
                      fontSize: 12,
                      wordWrap: 'on',
                      scrollBeyondLastLine: false,
                    }}
                  />
                </div>
              )}
            </div>

            {/* Sticky Action Footer */}
            <div style={{ padding: '16px 20px', borderTop: '1px solid var(--line-1)', background: 'rgba(0,0,0,0.2)', flexShrink: 0 }}>
              {/* Action Buttons */}
              <div style={{ display: 'flex', gap: 8, marginBottom: 8 }}>
                <button onClick={onSave} style={btnStyle('line')}>
                  <LucideSave size={14} /> 另存为
                </button>
                <button onClick={onReset} style={btnStyle('line')}>
                  <LucideRotateCcw size={14} /> 重置
                </button>
              </div>
              <button onClick={onRun} style={btnStyle('phos')}>
                🚀 进行仿真检查
              </button>
            </div>
          </div>
        )}
      </div>

      {/* TIER 2: Sidebar Rail (Fixed Floating Rail at right) */}
      <div style={{
        position: 'absolute', top: 20, right: 20,
        width: 72, 
        height: 'fit-content',
        flexShrink: 0, 
        display: 'flex', flexDirection: 'column',
        alignItems: 'center', paddingTop: 20, paddingBottom: 10, gap: 8,
        background: 'rgba(10, 15, 24, 0.9)',
        border: '1px solid var(--line-2)',
        borderRadius: 12,
        transition: 'all 0.2s',
        zIndex: 110
      }}>
        {TABS.map((tab) => (
          <button 
            key={tab.id} 
            title={tab.label} // Fallback for instant tooltip
            onClick={() => onActiveTabChange(activeTab === tab.id ? null : tab.id)} 
            style={{
              width: 48, height: 48, borderRadius: 8, border: 'none', cursor: 'pointer',
              background: activeTab === tab.id ? 'rgba(91,192,190,0.15)' : 'transparent',
              color: activeTab === tab.id ? 'var(--c-phos)' : 'var(--txt-3)',
              display: 'flex', alignItems: 'center', justifyContent: 'center',
              transition: 'all 0.2s',
              borderRight: activeTab === tab.id ? '3px solid var(--c-phos)' : '3px solid transparent',
              position: 'relative'
            }}
            className="rail-item-right"
          >
            {tab.icon}
            <style>{`
              .rail-item-right:hover::after {
                content: attr(title);
                position: absolute;
                right: 100%;
                margin-right: 12px;
                background: #0d131f;
                color: var(--txt-1);
                padding: 6px 12px;
                border-radius: 4px;
                font-size: 11px;
                white-space: nowrap;
                z-index: 1000;
                border: 1px solid var(--line-2);
                pointer-events: none;
                box-shadow: 0 4px 20px rgba(0,0,0,0.5);
              }
            `}</style>
          </button>
        ))}

        {activeTab && (
          <button title="Collapse panel" onClick={() => onActiveTabChange(null)} style={{
            marginTop: 12, marginBottom: 10, width: 48, height: 32, borderRadius: 4,
            border: '1px solid var(--line-2)', background: 'transparent',
            color: 'var(--txt-3)', cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center'
          }}>
            <LucideChevronRight size={18} />
          </button>
        )}
      </div>
    </>
  );
}

const btnStyle = (variant: 'line' | 'phos'): React.CSSProperties => ({
  flex: 1, padding: '12px 0', borderRadius: 4, cursor: 'pointer',
  fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 700,
  letterSpacing: '0.12em', textAlign: 'center',
  display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
  background: variant === 'phos' ? 'var(--c-phos)' : 'transparent',
  color: variant === 'phos' ? '#000' : 'var(--txt-1)',
  border: variant === 'phos' ? 'none' : '1px solid var(--line-2)',
  width: variant === 'phos' ? '100%' : undefined,
  transition: 'all 0.2s',
});