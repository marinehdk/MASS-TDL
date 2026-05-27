import { useState, useEffect, useMemo, useRef, useCallback } from 'react';
import maplibregl from 'maplibre-gl';
import { SilMapView } from '../map/SilMapView';
import { MapLayerSwitcher } from '../map/MapLayerSwitcher';
import * as jsyaml from 'js-yaml';
import {
  useListScenariosQuery,
  useGetScenarioQuery,
  useCreateScenarioMutation,
  useUpdateScenarioMutation,
} from '../api/silApi';
import { useScenarioStore } from '../store';
import { useSchemaValidation } from '../hooks/useSchemaValidation';
import { useMapInteraction } from '../hooks/useMapInteraction';
import {
  LucideCompass, LucideFolder, LucideChevronDown, LucideChevronRight, LucideSearch,
  LucideNavigation, LucideLock, LucidePencil, LucideMapPin, LucideShip, LucideChevronLeft
} from 'lucide-react';
import { BuilderRightRail, CollapsibleSection } from './shared/BuilderRightRail';

// ── ODD Select inline sub-component with Dynamic Suffix ────────────────
function ODDSelect({ label, value, onChange, options, suffix }: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  options: Array<{ value: string; label: string }>;
  suffix?: string;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
      <label style={{ fontSize: 13, fontWeight: 600, fontFamily: 'var(--f-mono)', color: 'var(--txt-2)' }}>{label}</label>
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        <select value={value} onChange={(e) => onChange(e.target.value)} style={{
          flex: 1,
          background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
          color: 'var(--txt-1)', padding: '8px 10px', borderRadius: 6,
          fontFamily: 'var(--f-mono)', fontSize: 14, outline: 'none',
          cursor: 'pointer'
        }}>
          {options.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
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
            fontWeight: 700,
            whiteSpace: 'nowrap',
            minWidth: '105px',
            textAlign: 'center'
          }}>
            {suffix}
          </div>
        )}
      </div>
    </div>
  );
}

// ── Left Sidebar Rail Configuration ──
const LEFT_TABS = [
  { id: 'odd',     label: '运行域', icon: <LucideCompass size={24} /> },
  { id: 'vessel',  label: '船型',   icon: <LucideShip size={24} /> },
  { id: 'library', label: '场景库', icon: <LucideFolder size={24} /> },
] as const;

type LeftTabId = typeof LEFT_TABS[number]['id'];

function SectionTitle({ title }: { title: string }) {
  return (
    <div style={{ 
      fontSize: 15, fontWeight: 700, color: 'var(--c-phos)', 
      fontFamily: 'var(--f-disp)', letterSpacing: '0.1em', 
      marginTop: 20, marginBottom: 14, display: 'flex', alignItems: 'center', gap: 8
    }}>
      <div style={{ width: 5, height: 15, background: 'var(--c-phos)', borderRadius: 2 }} />
      {title.toUpperCase()}
    </div>
  );
}

const btnStyle = (variant: 'line' | 'phos'): React.CSSProperties => ({
  flex: 1, padding: '12px 0', borderRadius: 6, cursor: 'pointer',
  fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 700,
  letterSpacing: '0.12em', textAlign: 'center',
  display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
  background: variant === 'phos' ? 'var(--c-phos)' : 'transparent',
  color: variant === 'phos' ? '#000' : 'var(--txt-1)',
  border: variant === 'phos' ? 'none' : '1px solid var(--line-2)',
  transition: 'all 0.2s',
});

export function SimulationScenario() {
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [yamlEditor, setYamlEditor] = useState('');
  const [expandedFolders, setExpandedFolders] = useState<Record<string, boolean>>({});
  const [searchTerm, setSearchTerm] = useState('');
  const [activeLeftTab, setActiveLeftTab] = useState<LeftTabId | null>('library');
  const [activeRightTab, setActiveRightTab] = useState<'environment' | 'vessels' | 'assertions' | 'raw' | null>(null);

  // Placement mode: 'none' | 'ownship' | 'target-0' | 'target-1' etc.
  const [placementMode, setPlacementMode] = useState<string>('none');
  const [substrate, setSubstrate] = useState<'enc' | 'sat' | 'osm'>('enc');

  // ── ODD Filter state ──
  const [oddDomain, setOddDomain] = useState<string>('open_sea_offshore_wind_farm');
  const [oddSeaState, setOddSeaState] = useState<string>('5');
  const [oddVisibility, setOddVisibility] = useState<string>('2.0');
  const [vesselClass, setVesselClass] = useState<string>('FCB-45m');

  const { data: scenarios = [] } = useListScenariosQuery();
  const { data: scenarioDetail } = useGetScenarioQuery(selectedId!, { skip: !selectedId });
  const schemaValidation = useSchemaValidation(yamlEditor);
  const [createScenario] = useCreateScenarioMutation();
  const [updateScenario] = useUpdateScenarioMutation();
  const setScenario = useScenarioStore((s) => s.setScenario);

  useEffect(() => {
    if (scenarioDetail && selectedId) {
      setScenario(selectedId, scenarioDetail.hash || '');
      let yamlStr = scenarioDetail.yaml_content;
      try {
        const doc = jsyaml.load(yamlStr) as any;
        if (doc && !doc.voyageTask) {
          // Auto-initialize voyageTask with sensible defaults
          const ownShip = doc.ownShip || {};
          const pos = ownShip.initial?.position || { latitude: 63.44, longitude: 10.38 };
          const heading = ownShip.initial?.heading || 0;
          const headingRad = (heading * Math.PI) / 180;
          
          // Project destination 2.0 nautical miles ahead (approx 0.033 degrees lat/lon)
          const destLat = Number((pos.latitude + 0.033 * Math.cos(headingRad)).toFixed(6));
          const destLon = Number((pos.longitude + (0.033 * Math.sin(headingRad)) / Math.cos((pos.latitude * Math.PI) / 180)).toFixed(6));
          
          // Compute default ETA (startTime + 10 minutes)
          let etaStr = '2026-05-15T05:20:00Z';
          if (doc.startTime) {
            try {
              const startDate = new Date(doc.startTime);
              startDate.setMinutes(startDate.getMinutes() + 10);
              etaStr = startDate.toISOString().split('.')[0] + 'Z';
            } catch {}
          }

          doc.voyageTask = {
            destination: {
              latitude: destLat,
              longitude: destLon
            },
            eta: etaStr,
            optimization_preference: '安全优先',
            waypoints: [
              { lat: pos.latitude, lon: pos.longitude },
              { lat: destLat, lon: destLon }
            ]
          };
          
          yamlStr = jsyaml.dump(doc);
        }
      } catch (e) {
        console.error('Failed to parse and auto-inject voyageTask', e);
      }
      setYamlEditor(yamlStr);
    }
  }, [scenarioDetail, selectedId]);

  // Synchronize ODD filter states and vessel class from loaded YAML
  useEffect(() => {
    try {
      const doc = jsyaml.load(yamlEditor) as any;
      if (doc?.metadata?.odd_cell) {
        const domain = doc.metadata.odd_cell.domain;
        const seaState = doc.metadata.odd_cell.sea_state_beaufort;
        const visibility = doc.metadata.odd_cell.visibility_nm;
        
        if (domain) setOddDomain(domain);
        if (seaState !== undefined) setOddSeaState(String(seaState));
        if (visibility !== undefined) setOddVisibility(String(visibility));
      }
      if (doc?.metadata?.vessel_class) {
        const vc = doc.metadata.vessel_class;
        if (vc === 'FCB' || vc === 'FCB-45m') {
          setVesselClass('FCB-45m');
        } else if (vc === 'ASD' || vc === 'ASD-28m' || vc === 'TUG-28m') {
          setVesselClass('ASD-28m');
        }
      }
    } catch (e) {
      // Ignore parsing errors
    }
  }, [selectedId, scenarioDetail]);

  // ODD change → write to yamlDoc.metadata.odd_cell (GAP-023)
  const lastOddRef = useRef('');
  useEffect(() => {
    const sig = `${oddDomain}|${oddSeaState}|${oddVisibility}`;
    if (sig === lastOddRef.current || !yamlEditor) return;
    lastOddRef.current = sig;
    handleUpdateYaml({
      'metadata.odd_cell.domain': oddDomain,
      'metadata.odd_cell.sea_state_beaufort': Number(oddSeaState),
      'metadata.odd_cell.visibility_nm': Number(oddVisibility),
    });
  }, [oddDomain, oddSeaState, oddVisibility]);

  // Vessel change → write to yamlDoc.metadata.vessel_class and model
  const lastVesselRef = useRef('');
  useEffect(() => {
    if (!yamlEditor) return;
    const vcValue = vesselClass === 'FCB-45m' ? 'FCB' : 'ASD';
    if (vcValue === lastVesselRef.current) return;
    lastVesselRef.current = vcValue;
    const modelValue = vesselClass === 'FCB-45m' ? 'fcb_mmg_vessel' : 'asd_tug_vessel';
    handleUpdateYaml({
      'metadata.vessel_class': vcValue,
      'ownShip.static.name': vesselClass === 'FCB-45m' ? 'FCB Own Ship' : 'ASD Own Ship',
      'ownShip.model': modelValue
    });
  }, [vesselClass]);

  // Parse YAML to get preview data
  const previewData = useMemo(() => {
    try {
      const doc = jsyaml.load(yamlEditor) as any;
      if (!doc) return null;

      const ownShip = doc.ownShip?.initial ? {
        lat: doc.ownShip.initial.position?.latitude ?? 0,
        lon: doc.ownShip.initial.position?.longitude ?? 0,
        heading: doc.ownShip.initial.heading ?? 0,
        cog: doc.ownShip.initial.cog ?? doc.ownShip.initial.heading ?? 0,
        sog: doc.ownShip.initial.sog ?? 0,
      } : undefined;

      const targets = doc.targetShips?.map((t: any, idx: number) => ({
        id: t.id || `target-${idx}`,
        lat: t.initial?.position?.latitude ?? 0,
        lon: t.initial?.position?.longitude ?? 0,
        heading: t.initial?.heading ?? 0,
        cog: t.initial?.cog ?? t.initial?.heading ?? 0,
        sog: t.initial?.sog ?? 0,
      })) || [];

      return { ownShip, targets };
    } catch (e) {
      return null;
    }
  }, [yamlEditor]);

  // ── Map interaction hook (Scheme B) ──
  const mapRef = useRef<maplibregl.Map | null>(null);
  const onYamlPatchRaw = useCallback((path: string, value: unknown) => {
    handleUpdateYaml({ [path]: value });
  }, []);

  const onYamlPatch = useMemo(() => {
    let timer: ReturnType<typeof setTimeout>;
    return (path: string, value: unknown) => {
      clearTimeout(timer);
      timer = setTimeout(() => onYamlPatchRaw(path, value), 200);
    };
  }, [onYamlPatchRaw]);

  const initialWpNodes = useMemo(() => {
    try {
      const doc = jsyaml.load(yamlEditor) as any;
      return (doc?.voyageTask?.waypoints || []).map((wp: any, idx: number) => ({
        idx,
        lon: wp.lon ?? 0,
        lat: wp.lat ?? 0,
      }));
    } catch { return []; }
  }, [yamlEditor]);

  useMapInteraction({
    mapRef,
    previewData,
    onYamlPatch,
    initialWpNodes,
  });

  // Generate Imazu geometry features
  const imazuGeometry = useMemo(() => {
    if (!previewData?.ownShip || !previewData.targets.length) return null;
    
    const features: any[] = [];
    const os = previewData.ownShip;

    previewData.targets.forEach((t: { id: string; lat: number; lon: number }) => {
      // Add a line between OS and Target
      features.push({
        type: 'Feature',
        geometry: { type: 'LineString', coordinates: [[os.lon, os.lat], [t.lon, t.lat]] },
        properties: { type: 'trajectory', title: 'Relative Distance' }
      });
    });

    return { type: 'FeatureCollection' as const, features };
  }, [previewData]);

  const handleSelect = (id: string) => {
    setSelectedId(id);
  };

  const isFolderExpanded = (folderId: string) => {
    return expandedFolders[folderId] === true;
  };

  const toggleFolder = (folderId: string) => {
    setExpandedFolders(prev => ({ ...prev, [folderId]: !prev[folderId] }));
  };

  const handleSave = async () => {
    if (!schemaValidation.valid) {
      const errList = schemaValidation.errors.slice(0, 5).join('\n• ');
      alert(`Cannot save — ${schemaValidation.errors.length} schema validation error(s):\n\n• ${errList}${schemaValidation.errors.length > 5 ? `\n\n...and ${schemaValidation.errors.length - 5} more` : ''}`);
      return;
    }
    try {
      const currentScenario = scenarios.find((s: any) => s.id === selectedId);
      if (currentScenario?.is_baseline || !selectedId) {
        const result = await createScenario(yamlEditor).unwrap();
        setSelectedId(result.scenario_id);
        setScenario(result.scenario_id, result.hash);
      } else {
        const result = await updateScenario({ id: selectedId, yaml_content: yamlEditor }).unwrap();
        setScenario(selectedId, result.hash);
      }
    } catch (err: any) {
      const msg = err?.data?.detail || err?.message || 'Save failed';
      alert(`Save error: ${msg}`);
      console.error(err);
    }
  };

  const handleRun = () => {
    if (selectedId) {
      const hash = scenarioDetail?.hash || '';
      setScenario(selectedId, hash);
      window.location.hash = `#/check/${selectedId}`;
    }
  };

  const handleReset = () => {
    if (scenarioDetail?.yaml_content) {
      setYamlEditor(scenarioDetail.yaml_content);
    }
  };

const handleUpdateYaml = useCallback((updates: any) => {
    try {
      const doc = jsyaml.load(yamlEditor) as any;
      if (!doc) return;

      Object.entries(updates).forEach(([key, value]) => {
        if (key.includes('.')) {
          const parts = key.split('.');
          let current = doc;
          for (let i = 0; i < parts.length - 1; i++) {
            if (!current[parts[i]]) current[parts[i]] = {};
            current = current[parts[i]];
          }
          current[parts[parts.length - 1]] = value;
        } else {
          doc[key] = value;
        }
      });

      setYamlEditor(jsyaml.dump(doc));
    } catch (e) {
      console.error('Failed to update YAML', e);
    }
  }, [yamlEditor]);

  const handleMapClick = useCallback((lon: number, lat: number) => {
    if (placementMode === 'none') return;
    handleUpdateYaml({
      [placementMode === 'ownship' ? 'ownShip.initial.position.latitude' : `targetShips.${placementMode.split('-')[1]}.initial.position.latitude`]: Number(lat.toFixed(6)),
      [placementMode === 'ownship' ? 'ownShip.initial.position.longitude' : `targetShips.${placementMode.split('-')[1]}.initial.position.longitude`]: Number(lon.toFixed(6)),
    });
    setPlacementMode('none');
  }, [placementMode, handleUpdateYaml]);

  // Handle keyboard shortcuts (1/2/3 tab switcher, ArrowRight to proceed, Cmd/Ctrl+S to save)
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      const activeEl = document.activeElement;
      if (activeEl && (
        activeEl.tagName === 'INPUT' || 
        activeEl.tagName === 'TEXTAREA' || 
        activeEl.getAttribute('contenteditable') === 'true' ||
        activeEl.classList.contains('input') ||
        activeEl.closest('.monaco-editor')
      )) {
        return;
      }

      if (e.key === '1') {
        setActiveLeftTab('odd');
      } else if (e.key === '2') {
        setActiveLeftTab('vessel');
      } else if (e.key === '3') {
        setActiveLeftTab('library');
      } else if (e.key === 'ArrowRight') {
        if (selectedId) {
          handleRun();
        }
      } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
        e.preventDefault();
        handleSave();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [selectedId, handleRun, handleSave]);

  // Dynamically group scenarios from the API response
  const suites = useMemo(() => {
    const groups: Record<string, any[]> = {};
    scenarios.forEach((s: any) => {
      const folderName = s.folder || 'Other';
      if (!groups[folderName]) groups[folderName] = [];
      groups[folderName].push({ ...s, displayName: s.id + '.yaml' });
    });

    return Object.entries(groups).map(([folderName, items]) => {
      items.sort((a, b) => a.displayName.localeCompare(b.displayName));
      const filtered = items.filter(item =>
         item.displayName.toLowerCase().includes(searchTerm.toLowerCase())
      );
      return { id: folderName, name: folderName, children: filtered };
    }).filter(suite => suite.children.length > 0);
  }, [scenarios, searchTerm]);

  // ── ODD-filtered scenario list ──
  const filteredSuites = useMemo(() => {
    return suites.map(suite => ({
      ...suite,
      children: suite.children.map((child: any) => {
        const oddCompatible = true;
        return { ...child, oddCompatible };
      }),
    }));
  }, [suites]);

  return (
    <div data-testid="simulation-scenario" style={{
      position: 'relative', width: '100%', height: '100%', overflow: 'hidden', background: '#070c13'
    }}>

      {/* BACKGROUND: Map View (full screen, z-index 0) */}
      <div style={{ position: 'absolute', top: 0, left: 0, width: '100%', height: '100%', zIndex: 0 }}>
        <SilMapView
          followOwnShip={false}
          viewMode="god"
          previewData={previewData || undefined}
          onMapClick={handleMapClick}
          substrate={substrate}
          geometry={imazuGeometry}
          mapRef={mapRef}
        />
      </div>

      {/* TIER 1: Left Content Panel (Independent Floating Module) */}
      <div style={{
        position: 'absolute', top: 20, left: 100, 
        width: '400px', 
        maxHeight: 'calc(100% - 40px)',
        height: 'auto',
        background: 'rgba(13, 19, 31, 0.95)', 
        backdropFilter: 'blur(16px)',
        border: '1px solid var(--line-2)',
        borderRadius: 12,
        display: 'flex', flexDirection: 'column',
        transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
        opacity: activeLeftTab ? 1 : 0,
        transform: activeLeftTab ? 'translateX(0)' : 'translateX(-20px)',
        pointerEvents: activeLeftTab ? 'auto' : 'none',
        zIndex: 105,
        boxShadow: activeLeftTab ? '20px 0 50px rgba(0,0,0,0.5)' : 'none',
        overflow: 'hidden'
      }}>
        {activeLeftTab && (
          <div style={{ display: 'flex', flexDirection: 'column', maxHeight: '100%', overflow: 'hidden', minHeight: 0 }}>
            {/* Panel Header */}
            <div style={{
              padding: '24px 20px 16px', borderBottom: '1px solid var(--line-1)',
              display: 'flex', justifyContent: 'center', alignItems: 'center',
              flexShrink: 0
            }}>
              <span style={{
                fontFamily: 'var(--f-disp)', fontSize: 15, fontWeight: 700,
                color: 'var(--txt-1)', letterSpacing: '0.2em'
              }}>
                {LEFT_TABS.find(t => t.id === activeLeftTab)?.label}
              </span>
            </div>

            {/* Panel Scrollable Content */}
            <div style={{ flex: 1, overflowY: 'auto', padding: '0 20px 30px', minHeight: 0 }}>
              
              {/* TAB 1: RUNNING DOMAIN (运行域) */}
              {activeLeftTab === 'odd' && (
                <div style={{ display: 'flex', flexDirection: 'column', gap: 16, paddingTop: 10 }}>
                  <SectionTitle title="场景运行域设置" />
                  <ODDSelect 
                    label="航行区域" 
                    value={oddDomain} 
                    onChange={setOddDomain}
                    options={[
                      { value: 'open_sea_offshore_wind_farm', label: '开阔水域' },
                      { value: 'coastal_archipelago', label: '近海群岛' },
                      { value: 'restricted_fairway', label: '限制航道' },
                      { value: 'harbour_approach', label: '港口水域' },
                    ]} 
                    suffix={
                      oddDomain === 'open_sea_offshore_wind_farm' ? 'Open Sea' :
                      oddDomain === 'coastal_archipelago' ? 'Coastal' :
                      oddDomain === 'restricted_fairway' ? 'Restricted' :
                      oddDomain === 'harbour_approach' ? 'Harbour' : ''
                    }
                  />
                  <ODDSelect 
                    label="风力/海况" 
                    value={oddSeaState} 
                    onChange={setOddSeaState}
                    options={[
                      { value: '3', label: '微浪' },
                      { value: '5', label: '轻浪' },
                      { value: '7', label: '巨浪' },
                      { value: '9', label: '狂涛' },
                    ]} 
                    suffix={`<= ${oddSeaState}`}
                  />
                  <ODDSelect 
                    label="能见度距离" 
                    value={oddVisibility} 
                    onChange={setOddVisibility}
                    options={[
                      { value: '0.5', label: '极差' },
                      { value: '1.0', label: '一般' },
                      { value: '2.0', label: '良好' },
                      { value: '5.0', label: '极佳' },
                    ]} 
                    suffix={`> ${Number(oddVisibility).toFixed(1)} nm`}
                  />
                </div>
              )}

              {/* TAB 2: VESSEL CLASS (船型) */}
              {activeLeftTab === 'vessel' && (
                <div style={{ display: 'flex', flexDirection: 'column', gap: 16, paddingTop: 10 }}>
                  <CollapsibleSection title="基础船型选择" defaultExpanded={true}>
                    <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
                      <ODDSelect 
                        label="船型选择" 
                        value={vesselClass} 
                        onChange={setVesselClass}
                        options={[
                          { value: 'FCB-45m', label: '快速运兵船' },
                          { value: 'ASD-28m', label: '全回转拖轮' },
                        ]} 
                        suffix={vesselClass === 'FCB-45m' ? 'FCB' : 'ASD'}
                      />
                      <div style={{
                        background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                        padding: '14px 16px', borderRadius: 8,
                      }}>
                        <div style={{ 
                          display: 'flex', 
                          alignItems: 'center', 
                          gap: 8, 
                          paddingBottom: 8, 
                          borderBottom: '1px dashed var(--line-1)',
                          marginBottom: 12 
                        }}>
                          <span style={{ fontSize: 18 }}>🚢</span>
                          <span style={{ 
                            fontFamily: 'var(--f-mono)', 
                            fontSize: 14, 
                            fontWeight: 600, 
                            color: 'var(--txt-1)',
                            marginLeft: 4
                          }}>
                            主要参数
                          </span>
                        </div>
                        <div style={{ 
                          fontFamily: 'var(--f-mono)', 
                          fontSize: 14, 
                          color: 'var(--txt-1)', 
                          lineHeight: 1.8,
                          display: 'flex',
                          flexDirection: 'column',
                          gap: 4
                        }}>
                          {vesselClass === 'FCB-45m' ? (
                            <>
                              <div>• 船型类别: 快速运兵船</div>
                              <div>• 总长: 45.00 m</div>
                              <div>• 两柱间长: 44.10 m</div>
                              <div>• 型宽: 8.00 m</div>
                              <div>• 型深: 3.85 m</div>
                              <div>• 设计吃水: 1.55 m</div>
                              <div>• 最大吃水: 2.00 m</div>
                              <div>• 最大航速: 25 节 @ 30吨载重</div>
                              <div>• 推进主机: 3台 康明斯 1350马力</div>
                              <div>• 首侧推器: 2台 10KN 侧推器</div>
                            </>
                          ) : (
                            <>
                              <div>• 船型类别: 全回转拖轮</div>
                              <div>• 总长: 28.00 m</div>
                              <div>• 两柱间长: 24.50 m</div>
                              <div>• 型宽: 9.80 m</div>
                              <div>• 型深: 4.60 m</div>
                              <div>• 设计吃水: 3.80 m</div>
                              <div>• 最大吃水: 4.20 m</div>
                              <div>• 最大航速: 12.5 节</div>
                              <div>• 推进主机: 2台 柴油主机 1800马力</div>
                              <div>• 推进装置: 双全回转舵桨</div>
                            </>
                          )}
                        </div>
                      </div>
                    </div>
                  </CollapsibleSection>
                  
                  <CollapsibleSection title="传感器配置" defaultExpanded={true}>
                    <div style={{
                      background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                      padding: '14px 16px', borderRadius: 8,
                    }}>
                      <div style={{ 
                        display: 'flex', 
                        alignItems: 'center', 
                        gap: 8, 
                        paddingBottom: 8, 
                        borderBottom: '1px dashed var(--line-1)',
                        marginBottom: 12 
                      }}>
                        <span style={{ fontSize: 18 }}>📡</span>
                        <span style={{ 
                          fontFamily: 'var(--f-mono)', 
                          fontSize: 14, 
                          fontWeight: 600, 
                          color: 'var(--txt-1)',
                          marginLeft: 4
                        }}>
                          传感器清单
                        </span>
                      </div>
                      <div style={{ 
                        fontFamily: 'var(--f-mono)', 
                        fontSize: 14, 
                        color: 'var(--txt-1)', 
                        lineHeight: 1.8,
                        display: 'flex',
                        flexDirection: 'column',
                        gap: 4
                      }}>
                        <div>• 航海雷达: X波段雷达</div>
                        <div>• 船载 AIS: AIS收发机</div>
                        <div>• 定位系统: 高精度 GNSS 定位仪</div>
                        <div>• 导航海图: 电子海图 system</div>
                      </div>
                    </div>
                  </CollapsibleSection>
                </div>
              )}

              {/* TAB 3: SCENARIO LIBRARY (场景库) */}
              {activeLeftTab === 'library' && (
                <div style={{ display: 'flex', flexDirection: 'column', height: '100%', paddingTop: 10 }}>
                  <SectionTitle title="测试资源库" />
                  
                  {/* Search bar + quick filters */}
                  <div style={{ marginBottom: 12 }}>
                    <div style={{ position: 'relative', display: 'flex', alignItems: 'center', marginBottom: 8 }}>
                      <LucideSearch size={14} color="var(--txt-3)" style={{ position: 'absolute', left: 10 }} />
                      <input
                        type="text" placeholder="搜索场景..." value={searchTerm}
                        onChange={(e) => setSearchTerm(e.target.value)}
                        style={{
                          width: '100%', background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)',
                          color: 'var(--txt-1)', padding: '8px 10px 8px 32px', fontFamily: 'var(--f-mono)', fontSize: 11,
                          outline: 'none', borderRadius: 6
                        }}
                      />
                    </div>
                    <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
                      {['多船', '含故障', '上次PASS', '上次FAIL'].map(tag => (
                        <span key={tag} style={{
                          padding: '2px 8px', borderRadius: 4,
                          background: 'rgba(91,192,190,0.08)', color: 'var(--txt-3)',
                          fontFamily: 'var(--f-mono)', fontSize: 9, border: '1px solid var(--line-1)',
                        }}>{tag}</span>
                      ))}
                    </div>
                  </div>

                  {/* Scenario Tree */}
                  <div style={{ flex: 1, overflowY: 'auto', maxHeight: '350px' }}>
                    {filteredSuites.map(suite => (
                      <div key={suite.id} style={{ marginBottom: 4 }}>
                        <div onClick={() => toggleFolder(suite.id)} style={{
                          display: 'flex', alignItems: 'center', gap: 6, padding: '6px 8px',
                          cursor: 'pointer', color: 'var(--txt-1)', borderRadius: 4,
                        }}>
                          {isFolderExpanded(suite.id) ? <LucideChevronDown size={12} /> : <LucideChevronRight size={12} />}
                          <LucideFolder size={12} color="#fa0" />
                          <span style={{ fontFamily: 'var(--f-body)', fontSize: 12, fontWeight: 500 }}>{suite.name}</span>
                          <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)', marginLeft: 'auto' }}>
                            {suite.children.length}
                          </span>
                        </div>
                        {isFolderExpanded(suite.id) && (
                          <div style={{ paddingLeft: 16 }}>
                            {suite.children.map((child: any) => (
                              <div key={child.id} onClick={() => handleSelect(child.id)} style={{
                                display: 'flex', alignItems: 'center', gap: 8, padding: '6px 10px', cursor: 'pointer',
                                background: selectedId === child.id ? 'rgba(91,192,190,0.12)' : 'transparent',
                                color: selectedId === child.id ? 'var(--c-phos)' : child.oddCompatible ? 'var(--txt-2)' : '#f87171',
                                borderRadius: 4, transition: 'all 0.1s',
                                borderLeft: `2px solid ${selectedId === child.id ? 'var(--c-phos)' : 'transparent'}`
                              }}>
                                <div style={{
                                  width: 6, height: 6, borderRadius: '50%',
                                  background: child.oddCompatible ? '#4ade80' : '#f87171',
                                }} />
                                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 11, flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                                  {child.name}
                                </span>
                                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)', padding: '1px 6px', background: 'rgba(0,0,0,0.2)', borderRadius: 3 }}>
                                  {child.encounter_type?.toUpperCase() || '—'}
                                </span>
                                {child.is_baseline ? (
                                  <span title="Baseline (只读)"><LucideLock size={10} color="var(--c-warn)" /></span>
                                ) : (
                                  <LucidePencil size={10} color="var(--txt-3)" />
                                )}
                              </div>
                            ))}
                          </div>
                        )}
                      </div>
                    ))}
                  </div>
                </div>
              )}

            </div>

            {/* Panel Footer containing the Confirm buttons */}
            <div style={{ 
              padding: '16px 20px', 
              borderTop: '1px solid var(--line-1)', 
              background: 'rgba(0,0,0,0.2)',
              display: 'flex',
              justifyContent: 'center',
              alignItems: 'center',
              flexShrink: 0
            }}>
              {activeLeftTab === 'odd' && (
                <button onClick={() => setActiveLeftTab('vessel')} style={{ ...btnStyle('phos'), flex: 'none', width: 120 }}>
                  下一步
                </button>
              )}
              {activeLeftTab === 'vessel' && (
                <div style={{ display: 'flex', gap: 12 }}>
                  <button onClick={() => setActiveLeftTab('odd')} style={{ ...btnStyle('line'), flex: 'none', width: 120 }}>
                    上一步
                  </button>
                  <button onClick={() => setActiveLeftTab('library')} style={{ ...btnStyle('phos'), flex: 'none', width: 120 }}>
                    下一步
                  </button>
                </div>
              )}
              {activeLeftTab === 'library' && (
                <div style={{ display: 'flex', gap: 12, width: '100%', justifyContent: 'center' }}>
                  <button onClick={() => setActiveLeftTab('vessel')} style={{ ...btnStyle('line'), flex: 'none', width: 120 }}>
                    上一步
                  </button>
                  <button 
                    onClick={() => {
                      setActiveLeftTab(null);
                      setActiveRightTab('vessels');
                    }} 
                    style={{ ...btnStyle('phos'), flex: 1, maxWidth: '180px' }} 
                    disabled={!selectedId}
                  >
                    {selectedId ? '确认场景' : '请在上方选择场景'}
                  </button>
                </div>
              )}
            </div>

          </div>
        )}
      </div>

      {/* TIER 2: Left Sidebar Rail (Fixed Floating Rail at left) */}
      <div style={{
        position: 'absolute', top: 20, left: 20,
        width: 72, 
        height: 'fit-content',
        flexShrink: 0, 
        display: 'flex', flexDirection: 'column',
        alignItems: 'center', paddingTop: 20, paddingBottom: 10, gap: 8,
        background: 'rgba(10, 15, 24, 0.95)',
        backdropFilter: 'blur(16px)',
        border: '1px solid var(--line-2)',
        borderRadius: 12,
        transition: 'all 0.2s',
        zIndex: 110
      }}>
        {LEFT_TABS.map((tab) => (
          <button 
            key={tab.id} 
            title={tab.label}
            onClick={() => setActiveLeftTab(prev => prev === tab.id ? null : tab.id)} 
            style={{
              width: 48, height: 48, borderRadius: 8, border: 'none', cursor: 'pointer',
              background: activeLeftTab === tab.id ? 'rgba(91,192,190,0.15)' : 'transparent',
              color: activeLeftTab === tab.id ? 'var(--c-phos)' : 'var(--txt-3)',
              display: 'flex', alignItems: 'center', justifyContent: 'center',
              transition: 'all 0.2s',
              borderLeft: activeLeftTab === tab.id ? '3px solid var(--c-phos)' : '3px solid transparent',
              position: 'relative'
            }}
            className="rail-item-left"
          >
            {tab.icon}
            <style>{`
              .rail-item-left:hover::after {
                content: attr(title);
                position: absolute;
                left: 100%;
                margin-left: 12px;
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

        {activeLeftTab && (
          <button title="收起面板" onClick={() => setActiveLeftTab(null)} style={{
            marginTop: 12, marginBottom: 10, width: 48, height: 32, borderRadius: 4,
            border: '1px solid var(--line-2)', background: 'transparent',
            color: 'var(--txt-3)', cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center'
          }}>
            <LucideChevronLeft size={18} />
          </button>
        )}
      </div>

      {/* CENTER: Bottom placement mode indicator + controls */}
      <div style={{
        position: 'absolute', bottom: 20, left: '50%', transform: 'translateX(-50%)',
        zIndex: 100, display: 'flex', alignItems: 'center', gap: 12,
        background: 'rgba(10, 15, 24, 0.85)', backdropFilter: 'blur(12px)',
        border: '1px solid var(--line-2)', borderRadius: 8, padding: '6px 16px',
      }}>
        {placementMode !== 'none' ? (
          <>
            <LucideNavigation size={14} color="var(--c-phos)" />
            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--c-phos)' }}>
              正在设置 {placementMode === 'ownship' ? '本船' : '目标船'} 位置...
            </span>
            <button onClick={() => setPlacementMode('none')} style={{
              background: 'rgba(255,255,255,0.1)', color: 'var(--txt-1)', border: 'none',
              padding: '2px 8px', borderRadius: 4, cursor: 'pointer', fontSize: 10
            }}>取消</button>
          </>
        ) : (
          <>
            <button onClick={() => setPlacementMode('ownship')} style={{
              background: 'rgba(91,192,190,0.1)', color: 'var(--c-phos)', border: '1px solid var(--line-1)',
              padding: '4px 12px', borderRadius: 4, cursor: 'pointer', fontSize: 10,
              fontFamily: 'var(--f-disp)', display: 'flex', alignItems: 'center', gap: 6
            }}>
              <LucideMapPin size={12} /> 放置本船
            </button>
            <button onClick={() => setPlacementMode(`target-${(previewData?.targets?.length || 0)}`)} style={{
              background: 'rgba(91,192,190,0.1)', color: 'var(--txt-1)', border: '1px solid var(--line-1)',
              padding: '4px 12px', borderRadius: 4, cursor: 'pointer', fontSize: 10,
              fontFamily: 'var(--f-disp)', display: 'flex', alignItems: 'center', gap: 6
            }}>
              <LucideMapPin size={12} /> 添加目标船
            </button>
          </>
        )}
      </div>

      {/* OVERLAY: Map Layer Switcher */}
      <div style={{ position: 'absolute', bottom: 68, right: 20, zIndex: 20 }}>
        <MapLayerSwitcher activeLayer={substrate} onLayerChange={setSubstrate} />
      </div>

      {/* RIGHT PANE: BuilderRightRail Inspector */}
      <BuilderRightRail
        yamlEditor={yamlEditor}
        onUpdateYaml={handleUpdateYaml}
        onChangeRawYaml={setYamlEditor}
        onRun={handleRun}
        onSave={handleSave}
        onReset={handleReset}
        isBaseline={selectedId ? scenarios.find((s: any) => s.id === selectedId)?.is_baseline : false}
        scenarioHash={scenarioDetail?.hash}
        activeTab={activeRightTab}
        onActiveTabChange={setActiveRightTab}
      />
    </div>
  );
}
