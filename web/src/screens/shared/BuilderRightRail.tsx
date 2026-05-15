import { useState, useMemo } from 'react';
import * as jsyaml from 'js-yaml';
import { useSchemaValidation } from '../../hooks/useSchemaValidation';
import { 
  LucideSettings2, LucideShip, LucideTarget, LucideCloudRain, LucideRadio, 
  LucideFileJson, LucideSave, LucideCheckCircle, LucidePlay, LucideChevronRight
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
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4, marginBottom: 16 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-end' }}>
        <label style={{ fontSize: 10, color: 'var(--txt-3)', fontFamily: 'var(--f-disp)', letterSpacing: '0.08em', textTransform: 'uppercase' }}>
          {label}
        </label>
        {unit && <span style={{ fontSize: 9, color: 'var(--txt-3)', fontFamily: 'var(--f-mono)' }}>{unit}</span>}
      </div>
      <input 
        type={type} 
        value={value ?? ''} 
        onChange={(e) => onChange(e.target.value)}
        style={{
          background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
          color: 'var(--txt-1)', padding: '8px 10px', borderRadius: 4,
          fontFamily: 'var(--f-mono)', fontSize: 12, outline: 'none', width: '100%',
          transition: 'border-color 0.2s'
        }}
        onFocus={(e) => e.target.style.borderColor = 'var(--c-phos)'}
        onBlur={(e) => e.target.style.borderColor = 'var(--line-1)'}
      />
      {description && <div style={{ fontSize: 9, color: 'var(--txt-3)', fontStyle: 'italic', marginTop: 2 }}>{description}</div>}
    </div>
  );
}

function Select({ label, value, onChange, options }: { label: string; value: string; onChange: (val: string) => void; options: string[] }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4, marginBottom: 16 }}>
      <label style={{ fontSize: 10, color: 'var(--txt-3)', fontFamily: 'var(--f-disp)', letterSpacing: '0.08em', textTransform: 'uppercase' }}>{label}</label>
      <select 
        value={value} 
        onChange={(e) => onChange(e.target.value)}
        style={{
          background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
          color: 'var(--txt-1)', padding: '8px 10px', borderRadius: 4,
          fontFamily: 'var(--f-mono)', fontSize: 12, outline: 'none', width: '100%',
          cursor: 'pointer'
        }}
      >
        {options.map((o) => <option key={o} value={o}>{o}</option>)}
      </select>
    </div>
  );
}

function SectionTitle({ title }: { title: string }) {
  return (
    <div style={{ 
      fontSize: 11, fontWeight: 700, color: 'var(--c-phos)', 
      fontFamily: 'var(--f-disp)', letterSpacing: '0.1em', 
      marginTop: 24, marginBottom: 12, display: 'flex', alignItems: 'center', gap: 8
    }}>
      <div style={{ width: 4, height: 12, background: 'var(--c-phos)', borderRadius: 2 }} />
      {title.toUpperCase()}
    </div>
  );
}

function BasicConfigTab({ doc, onUpdate }: { doc: any; onUpdate: (updates: any) => void }) {
  const metadata = doc?.metadata || {};
  const encounter = doc?.encounter || {};

  return (
    <div>
      <SectionTitle title="场景元数据" />
      <Field 
        label="场景名称" 
        value={doc?.title || doc?.name || ''} 
        onChange={(v) => onUpdate({ title: v })} 
      />
      <Field 
        label="描述" 
        value={doc?.description || ''} 
        onChange={(v) => onUpdate({ description: v })}
        description="场景的详细用途描述"
      />
      
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <Field label="Schema Version" value={metadata?.schema_version || ''} onChange={(v) => onUpdate({ 'metadata.schema_version': v })} />
        <Field label="Scenario ID" value={metadata?.scenario_id || ''} onChange={(v) => onUpdate({ 'metadata.scenario_id': v })} />
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <Field label="Source" value={metadata?.scenario_source || ''} onChange={(v) => onUpdate({ 'metadata.scenario_source': v })} />
        <Field label="ODD Zone" value={metadata?.odd_zone || ''} onChange={(v) => onUpdate({ 'metadata.odd_zone': v })} />
      </div>

      <SectionTitle title="遭遇类型 (COLREGs)" />
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <Select 
          label="遭遇类型" 
          value={encounter?.type || doc?.type || 'HO'} 
          onChange={(v) => onUpdate({ 'encounter.type': v, 'type': v })}
          options={['HO', 'CR_GW', 'CR_SO', 'OT', 'Custom']}
        />
        <Select 
          label="COLREGs 规则" 
          value={encounter?.rule || 'Rule14'} 
          onChange={(v) => onUpdate({ 'encounter.rule': v })}
          options={['Rule14', 'Rule15', 'Rule13', 'Rule16', 'Other']}
        />
      </div>

      <SectionTitle title="航行区域" />
      <Select 
        label="底图区域" 
        value={metadata?.geo_origin?.description || 'Norwegian Sea'} 
        onChange={(v) => onUpdate({ 'metadata.geo_origin.description': v })}
        options={['Norwegian Sea', 'Trondheim Fjord', 'Singapore Strait', 'Rotterdam']}
      />
    </div>
  );
}

function OwnShipConfigTab({ doc, onUpdate }: { doc: any; onUpdate: (updates: any) => void }) {
  const ownShip = doc?.own_ship || {};
  const pos = ownShip?.initial?.position || {};
  const initial = ownShip?.initial || {};
  const metadata = doc?.metadata || {};

  return (
    <div>
      <SectionTitle title="船舶基本信息" />
      <Field 
        label="船型名称" 
        value={metadata?.vessel_class || ownShip?.vessel_class || ''} 
        onChange={(v) => onUpdate({ 'metadata.vessel_class': v })} 
        description="本船的物理模型类别 (例如: FCB)"
      />

      <SectionTitle title="初始状态设置" />
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <Field 
          label="初始纬度" 
          value={pos?.latitude ?? ''} 
          onChange={(v) => onUpdate({ 'own_ship.initial.position.latitude': Number(v) })} 
          unit="LAT"
        />
        <Field 
          label="初始经度" 
          value={pos?.longitude ?? ''} 
          onChange={(v) => onUpdate({ 'own_ship.initial.position.longitude': Number(v) })} 
          unit="LON"
        />
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <Field 
          label="初始航向" 
          value={initial?.heading ?? pos?.heading ?? ''} 
          onChange={(v) => onUpdate({ 'own_ship.initial.heading': Number(v) })} 
          unit="°"
        />
        <Field 
          label="初始航速" 
          value={initial?.sog ?? pos?.speed ?? ''} 
          onChange={(v) => onUpdate({ 'own_ship.initial.sog': Number(v) })} 
          unit="kn"
          description="系统统一以 '节' (SOG) 为单位存储"
        />
      </div>
    </div>
  );
}

function TargetsConfigTab({ doc, onUpdate }: { doc: any; onUpdate: (updates: any) => void }) {
  const targets = doc?.target_ships || doc?.targets || [];

  return (
    <div style={{ paddingTop: 10 }}>
      {targets.length === 0 && (
        <div style={{ color: 'var(--txt-3)', padding: '40px 20px', textAlign: 'center', fontSize: 12 }}>
          当前场景无目标船数据
        </div>
      )}
      
      {targets.map((tgt: any, idx: number) => {
        const pos = tgt?.initial?.position || {};
        const initial = tgt?.initial || {};
        const prefix = `target_ships.${idx}`;

        return (
          <div key={idx} style={{ marginBottom: 40 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 20 }}>
              <span style={{ 
                background: 'var(--c-phos)', color: '#000', padding: '2px 8px', 
                borderRadius: 4, fontSize: 10, fontWeight: 800, fontFamily: 'var(--f-mono)' 
              }}>目标船</span>
              <span style={{ fontSize: 14, fontWeight: 700, color: 'var(--txt-1)', fontFamily: 'var(--f-mono)' }}>
                {tgt.id || 'Unknown'}
              </span>
              <div style={{ flex: 1, height: 1, background: 'var(--line-1)', opacity: 0.3 }} />
            </div>

            <SectionTitle title="目标船信息" />
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
              <Field label="Target ID" value={tgt.id || ''} onChange={(v) => onUpdate({ [`${prefix}.id`]: v })} />
              <Field label="MMSI" value={tgt.mmsi || ''} onChange={(v) => onUpdate({ [`${prefix}.mmsi`]: Number(v) })} />
            </div>

            <Select 
              label="运动控制模式" 
              value={tgt.control_mode || '固定航路'} 
              onChange={(v) => onUpdate({ [`${prefix}.control_mode`]: v })}
              options={['固定航路', '历史AIS', '仿真导入', 'NCDM', '自主模式']}
            />

            <SectionTitle title="初始状态设置" />
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

            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
              <Field 
                label="初始航向" 
                value={initial?.heading ?? pos?.heading ?? ''} 
                onChange={(v) => onUpdate({ [`${prefix}.initial.heading`]: Number(v) })} 
                unit="°"
              />
              <Field 
                label="初始航速" 
                value={initial?.sog ?? pos?.speed ?? tgt?.sog ?? ''} 
                onChange={(v) => onUpdate({ [`${prefix}.initial.sog`]: Number(v) })} 
                unit="kn"
              />
            </div>
          </div>
        );
      })}
    </div>
  );
}

const TABS = [
  { id: 'basic',       label: '基本配置',    icon: <LucideSettings2 size={24} /> },
  { id: 'ownship',     label: '本船配置',    icon: <LucideShip size={24} /> },
  { id: 'targets',     label: '目标船配置',  icon: <LucideTarget size={24} /> },
  { id: 'environment', label: '环境配置',    icon: <LucideCloudRain size={24} /> },
  { id: 'sensor',      label: '传感器配置',  icon: <LucideRadio size={24} /> },
  { id: 'ais',         label: 'AIS 数据',    icon: <LucideFileJson size={24} /> },
] as const;

type TabId = typeof TABS[number]['id'];

export interface BuilderRightRailProps {
  yamlEditor: string;
  onUpdateYaml: (updates: any) => void;
  previewData: any;
  onRun: () => void;
  onSave: () => void;
  onValidate: () => void;
}

export function BuilderRightRail({ yamlEditor, onUpdateYaml, onRun, onSave, onValidate }: BuilderRightRailProps) {
  const [activeTab, setActiveTab] = useState<TabId | null>(null);
  const validation = useSchemaValidation(yamlEditor);

  const doc = useMemo(() => {
    try {
      return jsyaml.load(yamlEditor) || {};
    } catch (e) {
      return {};
    }
  }, [yamlEditor]);

  const handleTabClick = (id: TabId) => {
    setActiveTab(prev => prev === id ? null : id);
  };

  return (
    <>
      {/* TIER 1: Content Panel (Independent Floating Module) */}
      <div style={{
        position: 'absolute', top: 20, right: 100, 
        width: '400px', 
        maxHeight: 'calc(100% - 120px)',
        height: 'fit-content',
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
          <div style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
            <div style={{
              padding: '24px 20px 16px', borderBottom: '1px solid var(--line-1)',
              display: 'flex', justifyContent: 'center', alignItems: 'center'
            }}>
              <span style={{
                fontFamily: 'var(--f-disp)', fontSize: 16, fontWeight: 700,
                color: 'var(--txt-1)', letterSpacing: '0.2em'
              }}>
                {TABS.find(t => t.id === activeTab)?.label}
              </span>
            </div>

            <div style={{
              display: 'flex', alignItems: 'center', gap: 8,
              padding: '8px 12px', borderRadius: 6,
              background: validation.valid ? 'rgba(0,255,0,0.06)' : 'rgba(255,60,60,0.10)',
              border: `1px solid ${validation.valid ? 'rgba(0,255,0,0.15)' : 'rgba(255,60,60,0.25)'}`,
              marginBottom: 8, marginTop: 4
            }}>
              <div style={{
                width: 8, height: 8, borderRadius: '50%',
                background: validation.valid ? '#4ade80' : '#f87171',
                boxShadow: `0 0 6px ${validation.valid ? 'rgba(74,222,128,0.4)' : 'rgba(248,113,113,0.4)'}`
              }} />
              <span style={{
                fontFamily: 'var(--f-mono, monospace)', fontSize: 11,
                color: validation.valid ? '#4ade80' : '#f87171',
                fontWeight: 500
              }}>
                {validation.valid ? 'Schema Valid ✓' : `Schema Errors: ${validation.errors.length}`}
              </span>
            </div>

            {!validation.valid && validation.errors.length > 0 && (
              <div style={{
                maxHeight: 110, overflowY: 'auto',
                background: 'rgba(255,60,60,0.04)', borderRadius: 4,
                padding: '8px 10px', marginBottom: 8,
                border: '1px solid rgba(255,60,60,0.08)'
              }}>
                {validation.errors.slice(0, 5).map((err, i) => (
                  <div key={i} style={{
                    fontFamily: 'var(--f-mono, monospace)', fontSize: 10,
                    color: '#fca5a5', lineHeight: 1.6,
                    wordBreak: 'break-word'
                  }}>✗ {err}</div>
                ))}
                {validation.errors.length > 5 && (
                  <div style={{ fontSize: 10, color: 'var(--txt-3, #666)', marginTop: 4, fontStyle: 'italic' }}>
                    ...and {validation.errors.length - 5} more errors
                  </div>
                )}
              </div>
            )}

            <div style={{ flex: 1, overflowY: 'auto', padding: '0 20px 40px' }}>
              {activeTab === 'basic' && <BasicConfigTab doc={doc} onUpdate={onUpdateYaml} />}
              {activeTab === 'ownship' && <OwnShipConfigTab doc={doc} onUpdate={onUpdateYaml} />}
              {activeTab === 'targets' && <TargetsConfigTab doc={doc} onUpdate={onUpdateYaml} />}
              {activeTab === 'environment' && <div style={{ color: 'var(--txt-3)', padding: 20 }}>Environment Configuration (WIP)</div>}
              {activeTab === 'sensor' && <div style={{ color: 'var(--txt-3)', padding: 20 }}>Sensor Configuration (WIP)</div>}
              {activeTab === 'ais' && <div style={{ color: 'var(--txt-3)', padding: 20 }}>AIS Data Configuration (WIP)</div>}
            </div>

            {/* Actions Footer */}
            <div style={{ padding: '16px 20px', borderTop: '1px solid var(--line-1)', background: 'rgba(0,0,0,0.2)' }}>
              <div style={{ display: 'flex', gap: 8, marginBottom: 8 }}>
                <button onClick={onSave} style={btnStyle('line')}>
                  <LucideSave size={14} /> SAVE
                </button>
                <button onClick={onValidate} style={btnStyle('line')}>
                  <LucideCheckCircle size={14} /> VALIDATE
                </button>
              </div>
              <button onClick={onRun} style={btnStyle('phos')}>
                RUN SCENARIO <LucidePlay size={14} />
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
            onClick={() => handleTabClick(tab.id)} 
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
          <button title="Collapse panel" onClick={() => setActiveTab(null)} style={{
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