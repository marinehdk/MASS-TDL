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
  LucideNavigation, LucideLock, LucidePencil, LucideMapPin
} from 'lucide-react';
import { BuilderRightRail } from './shared/BuilderRightRail';

// ── ODD Select inline sub-component ────────────────────────────────────
function ODDSelect({ label, value, onChange, options }: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  options: Array<{ value: string; label: string }>;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
      <label style={{ fontSize: 9, fontFamily: 'var(--f-mono)', color: 'var(--txt-3)' }}>{label}</label>
      <select value={value} onChange={(e) => onChange(e.target.value)} style={{
        background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
        color: 'var(--txt-1)', padding: '6px 8px', borderRadius: 4,
        fontFamily: 'var(--f-mono)', fontSize: 11, outline: 'none', width: '100%',
        cursor: 'pointer'
      }}>
        {options.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
      </select>
    </div>
  );
}

export function SimulationScenario() {
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [yamlEditor, setYamlEditor] = useState('');
  const [expandedFolders, setExpandedFolders] = useState<Record<string, boolean>>({});
  const [searchTerm, setSearchTerm] = useState('');

  // Placement mode: 'none' | 'ownship' | 'target-0' | 'target-1' etc.
  const [placementMode, setPlacementMode] = useState<string>('none');
  const [substrate, setSubstrate] = useState<'enc' | 'sat' | 'osm'>('enc');

  // ── ODD Filter state ──
  const [oddDomain, setOddDomain] = useState<string>('open_sea');
  const [oddSeaState, setOddSeaState] = useState<string>('5');
  const [oddVisibility, setOddVisibility] = useState<string>('2.0');
  const [vesselClass] = useState<string>('FCB-45m');

  const { data: scenarios = [] } = useListScenariosQuery();
  const { data: scenarioDetail } = useGetScenarioQuery(selectedId!, { skip: !selectedId });
  const schemaValidation = useSchemaValidation(yamlEditor);
  const [createScenario] = useCreateScenarioMutation();
  const [updateScenario] = useUpdateScenarioMutation();
  const setScenario = useScenarioStore((s) => s.setScenario);

  useEffect(() => {
    if (scenarioDetail && selectedId) {
      setYamlEditor(scenarioDetail.yaml_content);
    }
  }, [scenarioDetail, selectedId]);

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

    // Add a simple DCPA circle around OwnShip (e.g. 0.5nm)
    const circle = (lat: number, lon: number, radiusNm: number, title: string) => {
      const pts: [number, number][] = [];
      const d = radiusNm / 60;
      const cosLat = Math.cos(lat * Math.PI / 180);
      for (let i = 0; i <= 64; i++) {
        const a = (i / 64) * 2 * Math.PI;
        pts.push([lon + (d * Math.sin(a)) / cosLat, lat + d * Math.cos(a)]);
      }
      return {
        type: 'Feature',
        geometry: { type: 'LineString', coordinates: pts },
        properties: { type: 'dcpa', title }
      };
    };

    features.push(circle(os.lat, os.lon, 0.5, 'OS DCPA 0.5nm'));
    
    previewData.targets.forEach((t: { id: string; lat: number; lon: number }) => {
      features.push(circle(t.lat, t.lon, 0.5, `${t.id} DCPA 0.5nm`));
      
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

      {/* LEFT PANE: ODD Filter + Vessel Capability + Scenario Library */}
      <div style={{
        position: 'absolute', top: 20, left: 20, bottom: 20,
        width: '280px', zIndex: 100,
        display: 'flex', flexDirection: 'column', gap: 12,
      }}>
        {/* ODD Global Filter card */}
        <div style={{
          background: 'rgba(13, 19, 31, 0.95)',
          backdropFilter: 'blur(16px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12, padding: '16px',
        }}>
          <div style={{
            fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 700,
            color: 'var(--txt-1)', letterSpacing: '0.1em',
            marginBottom: 12, display: 'flex', alignItems: 'center', gap: 6
          }}>
            <LucideCompass size={14} color="var(--c-phos)" /> ODD 全局过滤器
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
            <ODDSelect label="航区域" value={oddDomain} onChange={setOddDomain}
              options={[
                { value: 'open_sea', label: 'Open Sea' },
                { value: 'coastal', label: 'Coastal' },
                { value: 'fairway', label: 'Fairway' },
                { value: 'port_entry', label: 'Port Entry' },
                { value: 'ofw', label: 'Offshore Wind Farm' },
              ]} />
            <ODDSelect label="海况" value={oddSeaState} onChange={setOddSeaState}
              options={[
                { value: '3', label: 'Beaufort ≤ 3' },
                { value: '5', label: 'Beaufort ≤ 5' },
                { value: '7', label: 'Beaufort ≤ 7' },
                { value: '9', label: 'Beaufort ≤ 9' },
              ]} />
            <ODDSelect label="能见度" value={oddVisibility} onChange={setOddVisibility}
              options={[
                { value: '0.5', label: '> 0.5 nm' },
                { value: '1.0', label: '> 1 nm' },
                { value: '2.0', label: '> 2 nm' },
                { value: '5.0', label: '> 5 nm' },
              ]} />
          </div>
        </div>

        {/* Vessel Capability Manifest */}
        <div style={{
          background: 'rgba(13, 19, 31, 0.95)',
          backdropFilter: 'blur(16px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12, padding: '12px 16px',
        }}>
          <div style={{ fontFamily: 'var(--f-disp)', fontSize: 10, color: 'var(--txt-2)', letterSpacing: '0.08em', marginBottom: 4 }}>
            船型能力清单
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 13, color: 'var(--c-phos)', fontWeight: 700 }}>
              🚢 {vesselClass}
            </span>
          </div>
          <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)', marginTop: 4 }}>
            吃水 3.2m · 旋回半径 340m · 制动 ≥0.6nm
          </div>
        </div>

        {/* Scenario Library */}
        <div style={{
          flex: 1, background: 'rgba(13, 19, 31, 0.95)',
          backdropFilter: 'blur(16px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12, display: 'flex', flexDirection: 'column',
          overflow: 'hidden',
        }}>
          {/* Search bar + quick filters */}
          <div style={{ padding: '14px 14px 10px' }}>
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

          {/* Scenario tree (keep existing tree logic, add baseline lock icons) */}
          <div style={{ flex: 1, overflowY: 'auto', padding: '0 10px 16px' }}>
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
                  <div style={{ paddingLeft: 20 }}>
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
      <MapLayerSwitcher activeLayer={substrate} onLayerChange={setSubstrate} />

      {/* RIGHT PANE: BuilderRightRail Inspector */}
      <BuilderRightRail
        yamlEditor={yamlEditor}
        onUpdateYaml={handleUpdateYaml}
        onChangeRawYaml={setYamlEditor}
        onRun={handleRun}
        onSave={handleSave}
        isBaseline={selectedId ? scenarios.find((s: any) => s.id === selectedId)?.is_baseline : false}
        scenarioHash={scenarioDetail?.hash}
      />
    </div>
  );
}
