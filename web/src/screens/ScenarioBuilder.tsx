import { useState, useEffect, useMemo } from 'react';
import { SilMapView } from '../map/SilMapView';
import { ImazuGeometry } from '../map/ImazuGeometry';
import { MapLayerSwitcher } from '../map/MapLayerSwitcher';
import * as jsyaml from 'js-yaml';
import {
  useListScenariosQuery,
  useGetScenarioQuery,
  useValidateScenarioMutation,
  useCreateScenarioMutation,
  useDeleteScenarioMutation,
} from '../api/silApi';
import { useScenarioStore } from '../store';
import { 
  LucidePlus, LucideSave, LucideCheckCircle, LucidePlay, 
  LucideCompass, LucideFolder, LucideFileJson, LucideChevronDown, LucideChevronRight, LucideSearch,
  LucideSettings2, LucideShip, LucideTarget, LucideCloudRain, LucideWaves, LucideRadio,
  LucideLayout, LucideDices, LucideBrainCircuit, LucidePanelLeftClose, LucidePanelLeftOpen,
  LucideMousePointer2, LucideNavigation, LucideArrowDown, LucideArrowUp
} from 'lucide-react';
import { BuilderRightRail } from './shared/BuilderRightRail';

type Domain = 'single' | 'mc' | 'rl';

export function ScenarioBuilder() {
  const [activeDomain, setActiveDomain] = useState<Domain>('single');
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [activeTab, setActiveTab] = useState<'basic' | 'ownship' | 'targets' | 'env' | 'sensor' | 'ais'>('basic');
  const [yamlEditor, setYamlEditor] = useState('');
  const [expandedFolders, setExpandedFolders] = useState<Record<string, boolean>>({});
  const [isRailExpanded, setIsRailExpanded] = useState(false);
  const [isExplorerVisible, setIsExplorerVisible] = useState(true);
  const [sortOrder, setSortOrder] = useState<'asc' | 'desc'>('asc');
  const [searchTerm, setSearchTerm] = useState('');

  // Placement mode: 'none' | 'ownship' | 'target-0' | 'target-1' etc.
  const [placementMode, setPlacementMode] = useState<string>('none');
  const [substrate, setSubstrate] = useState<'enc' | 'sat' | 'osm'>('enc');

  const { data: scenarios = [] } = useListScenariosQuery();
  const { data: scenarioDetail } = useGetScenarioQuery(selectedId!, { skip: !selectedId });
  const [validate, { data: validation }] = useValidateScenarioMutation();
  const [createScenario] = useCreateScenarioMutation();
  const setScenario = useScenarioStore((s) => s.setScenario);

  useEffect(() => {
    if (scenarioDetail && selectedId) {
      setYamlEditor(scenarioDetail.yaml_content);
    }
  }, [scenarioDetail, selectedId]);

  // Parse YAML to get preview data
  const previewData = useMemo(() => {
    try {
      const doc = jsyaml.load(yamlEditor) as any;
      if (!doc) return null;

      const ownShip = doc.own_ship?.initial ? {
        lat: doc.own_ship.initial.position?.latitude ?? 0,
        lon: doc.own_ship.initial.position?.longitude ?? 0,
        heading: doc.own_ship.initial.heading ?? 0,
        cog: doc.own_ship.initial.cog ?? doc.own_ship.initial.heading ?? 0,
        sog: doc.own_ship.initial.sog ?? 0,
      } : undefined;

      const targets = doc.target_ships?.map((t: any, idx: number) => ({
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
    
    previewData.targets.forEach(t => {
      features.push(circle(t.lat, t.lon, 0.5, `${t.id} DCPA 0.5nm`));
      
      // Add a line between OS and Target
      features.push({
        type: 'Feature',
        geometry: { type: 'LineString', coordinates: [[os.lon, os.lat], [t.lon, t.lat]] },
        properties: { type: 'trajectory', title: 'Relative Distance' }
      });
    });

    return { type: 'FeatureCollection', features };
  }, [previewData]);

  const handleSelect = (id: string) => {
    setSelectedId(id);
  };

  const isFolderExpanded = (folderId: string) => {
    return expandedFolders[folderId] === true;
  };

  const toggleFolder = (folderId: string) => {
    setExpandedFolders(prev => ({ ...prev, [folderId]: prev[folderId] === false ? true : false }));
  };

  const handleValidate = () => validate(yamlEditor);

  const handleSave = async () => {
    try {
      const result = await createScenario(yamlEditor).unwrap();
      setScenario(result.scenario_id, result.hash);
      alert('Scenario saved!');
    } catch (err) {
      console.error(err);
    }
  };

  const handleRun = () => {
    if (selectedId) {
      const hash = scenarioDetail?.hash || '';
      setScenario(selectedId, hash);
      window.location.hash = `#/preflight/${selectedId}`;
    }
  };

  const handleUpdateYaml = (updates: any) => {
    try {
      const doc = jsyaml.load(yamlEditor) as any;
      if (!doc) return;

      // Deep merge updates into doc
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
  };

  const handleMapClick = (lon: number, lat: number) => {
    if (placementMode === 'none') return;
    handleUpdateYaml({
      [placementMode === 'ownship' ? 'own_ship.initial.position.latitude' : `target_ships.${placementMode.split('-')[1]}.initial.position.latitude`]: Number(lat.toFixed(6)),
      [placementMode === 'ownship' ? 'own_ship.initial.position.longitude' : `target_ships.${placementMode.split('-')[1]}.initial.position.longitude`]: Number(lon.toFixed(6)),
    });
    setPlacementMode('none');
  };

  // Dynamically group scenarios from the API response
  const suites = useMemo(() => {
    const groups: Record<string, any[]> = {};
    scenarios.forEach((s: any) => {
      const folderName = s.folder || 'Other';
      if (!groups[folderName]) groups[folderName] = [];
      groups[folderName].push({ ...s, displayName: s.id + '.yaml' });
    });

    return Object.entries(groups).map(([folderName, items]) => {
      items.sort((a, b) => {
        const val = a.displayName.localeCompare(b.displayName);
        return sortOrder === 'asc' ? val : -val;
      });
      
      const filtered = items.filter(item => 
         item.displayName.toLowerCase().includes(searchTerm.toLowerCase())
      );

      return {
        id: folderName,
        name: folderName,
        children: filtered
      };
    }).filter(suite => suite.children.length > 0);
  }, [scenarios, sortOrder, searchTerm]);

  return (
    <div data-testid="scenario-builder" style={{ 
      position: 'relative', width: '100%', height: '100%', overflow: 'hidden', background: '#070c13' 
    }}>
      
      {/* BACKGROUND: Map View (Fixed, no stretching) */}
      <div style={{ position: 'absolute', top: 0, left: 0, width: '100%', height: '100%', zIndex: 0 }}>
        <SilMapView 
          followOwnShip={false} 
          viewMode="god" 
          previewData={previewData || undefined}
          onMapClick={handleMapClick}
          substrate={substrate}
          geometry={imazuGeometry}
        />
      </div>

      {/* TIER 1: Leftmost Nav Rail (Floating Overlay) */}
      <div 
        style={{
          position: 'absolute', top: 20, left: 20,
          width: '72px', 
          height: 'fit-content',
          background: 'rgba(10, 15, 24, 0.9)', 
          backdropFilter: 'blur(12px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12,
          display: 'flex', flexDirection: 'column',
          zIndex: 110,
          paddingBottom: 10
        }}
      >
        <div style={{ padding: '20px 10px', display: 'flex', flexDirection: 'column', gap: 8 }}>
          {/* Domain 1: Single Scenario - TOGGLES EXPLORER */}
          <div 
            title="单一场景 (SINGLE)"
            onClick={() => setIsExplorerVisible(!isExplorerVisible)}
            style={{ 
              display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '12px',
              background: activeDomain === 'single' ? 'rgba(91, 192, 190, 0.1)' : 'transparent',
              borderRadius: 8, cursor: 'pointer',
              color: activeDomain === 'single' ? 'var(--c-phos)' : 'var(--txt-3)',
              borderLeft: activeDomain === 'single' ? '3px solid var(--c-phos)' : '3px solid transparent',
              transition: 'all 0.2s',
              position: 'relative'
            }}
            className="rail-item"
          >
            <LucideLayout size={24} />
            <style>{`
              .rail-item:hover::after {
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
                z-index: 200;
                border: 1px solid var(--line-2);
                pointer-events: none;
                box-shadow: 0 4px 20px rgba(0,0,0,0.5);
              }
            `}</style>
          </div>

          {/* Domain 2: Monte Carlo - LOCKED */}
          <div 
            title="蒙特卡洛 (MC RUNS) - 锁定"
            style={{ 
              display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '12px',
              borderRadius: 8, cursor: 'not-allowed', opacity: 0.2,
              color: 'var(--txt-3)', borderLeft: '3px solid transparent'
            }}
          >
            <LucideDices size={24} />
          </div>

          {/* Domain 3: RL - LOCKED */}
          <div 
            title="强化学习 (RL AGENT) - 锁定"
            style={{ 
              display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '12px',
              borderRadius: 8, cursor: 'not-allowed', opacity: 0.2,
              color: 'var(--txt-3)', borderLeft: '3px solid transparent'
            }}
          >
            <LucideBrainCircuit size={24} />
          </div>
        </div>
      </div>

      {/* TIER 2: Scenario Explorer Drawer (Absolute Overlay) */}
      <div style={{
        position: 'absolute', top: 20, left: 100,
        width: '260px', 
        maxHeight: 'calc(100% - 120px)',
        height: 'fit-content',
        background: 'rgba(13, 19, 31, 0.95)', 
        backdropFilter: 'blur(16px)',
        border: '1px solid var(--line-2)',
        borderRadius: 12,
        display: 'flex', flexDirection: 'column',
        transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
        opacity: isExplorerVisible ? 1 : 0,
        transform: isExplorerVisible ? 'translateX(0)' : 'translateX(-20px)',
        pointerEvents: isExplorerVisible ? 'auto' : 'none',
        zIndex: 105,
        boxShadow: isExplorerVisible ? '20px 0 50px rgba(0,0,0,0.5)' : 'none',
        overflow: 'hidden'
      }}>
        {/* Header with Search & New */}
        <div style={{ padding: '24px 16px 16px', minWidth: 260 }}>
          <div style={{ display: 'flex', alignItems: 'center', marginBottom: 20 }}>
            {/* Centered Title */}
            <div style={{ 
              flex: 1, textAlign: 'center', 
              fontFamily: 'var(--f-disp)', fontSize: 16, fontWeight: 700, 
              color: 'var(--txt-1)', letterSpacing: 4 
            }}>场景库</div>
          </div>
          
          <div style={{ position: 'relative', display: 'flex', alignItems: 'center' }}>
            <LucideSearch size={14} color="var(--txt-3)" style={{ position: 'absolute', left: 10 }} />
            <input 
              type="text" 
              placeholder="搜索场景名称" 
              value={searchTerm}
              onChange={(e) => setSearchTerm(e.target.value)}
              style={{
                width: '100%', background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)', 
                color: 'var(--txt-1)', padding: '10px 12px 10px 36px', fontFamily: 'var(--f-mono)', fontSize: 12,
                outline: 'none', borderRadius: 6
              }} 
            />
          </div>
        </div>

        {/* Tree View - Scrollable */}
        <div style={{ flex: 1, overflowY: 'auto', padding: '0 12px 24px', minWidth: 260 }}>
          {suites.map(suite => (
            <div key={suite.id} style={{ marginBottom: 6 }}>
              <div onClick={() => toggleFolder(suite.id)} style={{ 
                display: 'flex', alignItems: 'center', gap: 8, padding: '8px 10px', 
                cursor: 'pointer', color: 'var(--txt-1)', borderRadius: 6,
                background: 'rgba(255,255,255,0.02)'
              }}>
                {isFolderExpanded(suite.id) ? <LucideChevronDown size={14} /> : <LucideChevronRight size={14} />}
                <LucideFolder size={14} color="#fa0" fill="#fa02" />
                <span style={{ fontFamily: 'var(--f-body)', fontSize: 13, fontWeight: 500 }}>{suite.name}</span>
              </div>
              {isFolderExpanded(suite.id) && (
                <div style={{ paddingLeft: 22, marginTop: 4 }}>
                  {suite.children.map((child: any) => (
                    <div key={child.id} onClick={() => handleSelect(child.id)} style={{
                      display: 'flex', alignItems: 'center', gap: 10, padding: '8px 12px', cursor: 'pointer',
                      background: selectedId === child.id ? 'rgba(91,192,190,0.15)' : 'transparent',
                      color: selectedId === child.id ? 'var(--c-phos)' : 'var(--txt-2)',
                      borderRadius: 4, marginBottom: 2, transition: 'all 0.1s',
                      borderLeft: `2px solid ${selectedId === child.id ? 'var(--c-phos)' : 'transparent'}`
                    }}>
                      <LucideFileJson size={14} />
                      <span style={{ fontFamily: 'var(--f-mono)', fontSize: 12 }}>{child.displayName}</span>
                    </div>
                  ))}
                </div>
              )}
            </div>
          ))}
        </div>
      </div>

      {/* OVERLAY UI ELEMENTS */}
      <MapLayerSwitcher activeLayer={substrate} onLayerChange={setSubstrate} />
      
      <div className="glass-panel" style={{
        position: 'absolute', top: 20, left: isExplorerVisible ? 380 : 110, padding: '0 16px',
        display: 'flex', alignItems: 'center', gap: 12, zIndex: 110, height: 38,
        borderRadius: 4, border: '1px solid var(--line-1)',
        transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={{ fontSize: 11, color: 'var(--txt-2)', fontFamily: 'var(--f-body)', fontWeight: 500 }}>当前运行域：</span>
          <span style={{ fontFamily: 'var(--f-disp)', fontSize: 13, color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.05em' }}>单个场景</span>
        </div>
      </div>

      {/* Placement Mode Indicator */}
      {placementMode !== 'none' && (
        <div style={{
          position: 'absolute', bottom: 40, left: '50%', transform: 'translateX(-50%)',
          padding: '12px 24px', background: 'var(--c-phos)', color: '#000', borderRadius: 30,
          display: 'flex', alignItems: 'center', gap: 12, fontWeight: 800, fontSize: 14,
          boxShadow: '0 8px 32px rgba(91,192,190,0.4)', zIndex: 1000
        }}>
          <LucideNavigation size={20} className="animate-pulse" />
          <span>正在设置 {placementMode === 'ownship' ? '本船' : '目标船'} 位置... 点击地图确认</span>
          <button 
            onClick={() => setPlacementMode('none')}
            style={{ background: '#000', color: '#fff', border: 'none', padding: '4px 12px', borderRadius: 4, cursor: 'pointer', marginLeft: 12 }}
          >
            取消
          </button>
        </div>
      )}

      <BuilderRightRail
        yamlEditor={yamlEditor}
        onUpdateYaml={handleUpdateYaml}
        previewData={previewData}
        onRun={handleRun}
        onSave={handleSave}
        onValidate={handleValidate}
      />
    </div>
  );
}
