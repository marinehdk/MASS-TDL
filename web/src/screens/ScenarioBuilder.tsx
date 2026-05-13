import { useState, useEffect } from 'react';
import { SilMapView } from '../map/SilMapView';
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
  LucideLayout, LucideDices, LucideBrainCircuit, LucidePanelLeftClose, LucidePanelLeftOpen
} from 'lucide-react';

type Domain = 'single' | 'mc' | 'rl';

export function ScenarioBuilder() {
  const [activeDomain, setActiveDomain] = useState<Domain>('single');
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [activeTab, setActiveTab] = useState<'basic' | 'ownship' | 'targets' | 'env' | 'sensor' | 'ais'>('basic');
  const [yamlEditor, setYamlEditor] = useState('');
  const [expandedFolders, setExpandedFolders] = useState<Record<string, boolean>>({ 'imazu': true });
  const [isRailExpanded, setIsRailExpanded] = useState(false);
  const [isExplorerVisible, setIsExplorerVisible] = useState(true);

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

  const handleSelect = (id: string) => {
    setSelectedId(id);
  };

  const toggleFolder = (folderId: string) => {
    setExpandedFolders(prev => ({ ...prev, [folderId]: !prev[folderId] }));
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

  // Dynamically group scenarios from the API response (No hardcoding)
  const imazuChildren = scenarios
    .filter((s: any) => s.id?.includes('imazu') || s.name?.toLowerCase().includes('imazu'))
    .map((s: any) => {
      const match = (s.id || s.name).match(/imazu-\d+/i);
      return { ...s, name: match ? match[0].toLowerCase() : s.name, type: 'scenario' };
    });

  const colregsChildren = scenarios
    .filter((s: any) => s.id?.includes('colreg') || s.name?.toLowerCase().includes('colreg'))
    .map((s: any) => {
      const match = (s.id || s.name).match(/colreg-rule\d+/i);
      return { ...s, name: match ? match[0].toLowerCase() : s.name, type: 'scenario' };
    })
    .slice(0, 3); // 仅保留 3 个

  const suites = [
    { id: 'imazu', name: 'IMAZU标准测试', children: imazuChildren },
    { id: 'colregs', name: 'COLREGs测试', children: colregsChildren }
  ];

  return (
    <div style={{ display: 'flex', width: '100%', height: '100%', overflow: 'hidden', background: '#070c13' }}>
      
      {/* TIER 1: Leftmost Nav Rail */}
      <div 
        onMouseEnter={() => setIsRailExpanded(true)}
        onMouseLeave={() => setIsRailExpanded(false)}
        style={{
          width: isRailExpanded ? '200px' : '72px', 
          background: '#0a0f18', 
          borderRight: '1px solid var(--line-2)',
          display: 'flex', flexDirection: 'column',
          transition: 'width 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
          zIndex: 110,
          flexShrink: 0
        }}
      >
        <div style={{ padding: '20px 10px', display: 'flex', flexDirection: 'column', gap: 12 }}>
          {/* Domain 1: Single Scenario */}
          <div style={{ 
            display: 'flex', alignItems: 'center', gap: 16, padding: '12px',
            background: activeDomain === 'single' ? 'rgba(91, 192, 190, 0.1)' : 'transparent',
            borderRadius: 8, cursor: 'pointer',
            color: activeDomain === 'single' ? 'var(--c-phos)' : 'var(--txt-3)',
            borderLeft: activeDomain === 'single' ? '3px solid var(--c-phos)' : '3px solid transparent',
            overflow: 'hidden'
          }}>
            <LucideLayout size={24} style={{ minWidth: 24 }} />
            <div style={{ display: 'flex', flexDirection: 'column', whiteSpace: 'nowrap', opacity: isRailExpanded ? 1 : 0, transition: 'opacity 0.2s' }}>
              <span style={{ fontSize: 14, fontWeight: 700 }}>单一场景</span>
              <span style={{ fontSize: 9, opacity: 0.6, fontFamily: 'var(--f-mono)' }}>SINGLE</span>
            </div>
          </div>

          {/* Domain 2: Monte Carlo - LOCKED */}
          <div style={{ 
            display: 'flex', alignItems: 'center', gap: 16, padding: '12px',
            borderRadius: 8, cursor: 'not-allowed', opacity: 0.2,
            color: 'var(--txt-3)', borderLeft: '3px solid transparent', overflow: 'hidden'
          }}>
            <LucideDices size={24} style={{ minWidth: 24 }} />
            <div style={{ display: 'flex', flexDirection: 'column', whiteSpace: 'nowrap', opacity: isRailExpanded ? 1 : 0 }}>
              <span style={{ fontSize: 14, fontWeight: 700 }}>蒙特卡洛</span>
              <span style={{ fontSize: 9, opacity: 0.6, fontFamily: 'var(--f-mono)' }}>MC RUNS</span>
            </div>
          </div>

          {/* Domain 3: RL - LOCKED */}
          <div style={{ 
            display: 'flex', alignItems: 'center', gap: 16, padding: '12px',
            borderRadius: 8, cursor: 'not-allowed', opacity: 0.2,
            color: 'var(--txt-3)', borderLeft: '3px solid transparent', overflow: 'hidden'
          }}>
            <LucideBrainCircuit size={24} style={{ minWidth: 24 }} />
            <div style={{ display: 'flex', flexDirection: 'column', whiteSpace: 'nowrap', opacity: isRailExpanded ? 1 : 0 }}>
              <span style={{ fontSize: 14, fontWeight: 700 }}>强化学习</span>
              <span style={{ fontSize: 9, opacity: 0.6, fontFamily: 'var(--f-mono)' }}>RL AGENT</span>
            </div>
          </div>
        </div>
      </div>

      {/* TIER 2: Scenario Explorer Drawer */}
      <div style={{
        width: isExplorerVisible ? '260px' : '0px', 
        background: '#0d131f', 
        borderRight: isExplorerVisible ? '1px solid var(--line-2)' : 'none',
        display: 'flex', flexDirection: 'column',
        flexShrink: 0,
        transition: 'width 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
        overflow: 'hidden',
        position: 'relative'
      }}>
        {/* Toggle Button for Drawer (Floating or at top) */}
        {!isExplorerVisible && (
          <button 
            onClick={() => setIsExplorerVisible(true)}
            style={{ 
              position: 'absolute', top: 20, left: 10, zIndex: 120,
              background: 'var(--bg-1)', border: '1px solid var(--line-2)', color: 'var(--c-phos)',
              padding: 6, borderRadius: 4, cursor: 'pointer'
            }}
          >
            <LucidePanelLeftOpen size={18} />
          </button>
        )}

        {/* Header with Search & New */}
        <div style={{ padding: '24px 16px 16px', minWidth: 260 }}>
          <div style={{ display: 'flex', alignItems: 'center', marginBottom: 20 }}>
            {/* Collapse Button */}
            <button 
              onClick={() => setIsExplorerVisible(false)}
              style={{ background: 'transparent', border: 'none', color: 'var(--txt-3)', cursor: 'pointer', padding: 0 }}
            >
              <LucidePanelLeftClose size={18} />
            </button>
            
            {/* Centered Title */}
            <div style={{ 
              flex: 1, textAlign: 'center', 
              fontFamily: 'var(--f-disp)', fontSize: 14, fontWeight: 700, 
              color: 'var(--txt-1)', letterSpacing: 2 
            }}>场景库</div>

            {/* Action Buttons */}
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <button style={{ 
                background: 'rgba(91,192,190,0.1)', border: '1px solid var(--line-1)', 
                color: 'var(--c-phos)', fontSize: 10, fontWeight: 700,
                padding: '4px 8px', borderRadius: 4, cursor: 'pointer',
                display: 'flex', alignItems: 'center', gap: 4
              }}>
                新建+
              </button>
            </div>
          </div>
          
          <div style={{ position: 'relative', display: 'flex', alignItems: 'center' }}>
            <LucideSearch size={14} color="var(--txt-3)" style={{ position: 'absolute', left: 10 }} />
            <input type="text" placeholder="搜索场景名称" style={{
              width: '100%', background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)', 
              color: 'var(--txt-1)', padding: '10px 12px 10px 36px', fontFamily: 'var(--f-mono)', fontSize: 12,
              outline: 'none', borderRadius: 6
            }} />
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
                {expandedFolders[suite.id] ? <LucideChevronDown size={14} /> : <LucideChevronRight size={14} />}
                <LucideFolder size={14} color="#fa0" fill="#fa02" />
                <span style={{ fontFamily: 'var(--f-body)', fontSize: 13, fontWeight: 500 }}>{suite.name}</span>
              </div>
              {expandedFolders[suite.id] && (
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
                      <span style={{ fontFamily: 'var(--f-mono)', fontSize: 12 }}>{child.name}</span>
                    </div>
                  ))}
                </div>
              )}
            </div>
          ))}
        </div>
      </div>

      {/* CENTER PANEL: Map View */}
      <div style={{ flex: 1, position: 'relative', background: '#0a0c10' }}>
        {/* Toggle button overlay when explorer is hidden */}
        {!isExplorerVisible && (
          <button 
            onClick={() => setIsExplorerVisible(true)}
            style={{ 
              position: 'absolute', top: 20, left: 20, zIndex: 120,
              background: 'var(--bg-1)', border: '1px solid var(--line-2)', color: 'var(--txt-3)',
              padding: '6px 8px', borderRadius: 4, cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 6,
              boxShadow: '0 4px 12px rgba(0,0,0,0.5)', fontFamily: 'var(--f-disp)', fontSize: 11
            }}
          >
            <LucidePanelLeftOpen size={16} /> 展开场景库
          </button>
        )}

        <SilMapView followOwnShip={false} viewMode="god" />
        
        <div className="glass-panel" style={{
          position: 'absolute', top: 20, right: 20, padding: '12px 16px',
          display: 'flex', gap: 16, alignItems: 'center'
        }}>
          <div>
            <div style={{ fontSize: 10, color: 'var(--txt-3)', marginBottom: 4 }}>OPERATIONAL DOMAIN</div>
            <div style={{ fontFamily: 'var(--f-disp)', fontSize: 13, color: 'var(--c-phos)', fontWeight: 700 }}>单一场景 / SINGLE SCENARIO</div>
          </div>
        </div>
      </div>

      {/* RIGHT PANEL: 10% - Parameters */}
      <div style={{ width: '280px', background: '#0a0f18', display: 'flex', flexDirection: 'column', borderLeft: '1px solid var(--line-2)' }}>
        <div style={{ 
          padding: '16px', borderBottom: '1px solid var(--line-1)', 
          fontFamily: 'var(--f-disp)', fontSize: 14, fontWeight: 700, color: 'var(--txt-0)',
          display: 'flex', alignItems: 'center', gap: 8
        }}>
          <LucideSettings2 size={16} color="var(--c-phos)" />
          <span>PARAMETERS</span>
        </div>

        <div style={{ flex: 1, overflowY: 'auto', padding: '12px' }}>
          {[
            { id: 'basic', label: 'Basic Configuration', icon: <LucideSettings2 size={14} /> },
            { id: 'ownship', label: 'Own Ship Specs', icon: <LucideShip size={14} /> },
            { id: 'targets', label: 'Target Tracking', icon: <LucideTarget size={14} /> },
            { id: 'env', label: 'Metocean Data', icon: <LucideCloudRain size={14} /> },
          ].map(section => (
            <div 
              key={section.id} 
              onClick={() => setActiveTab(section.id as any)}
              style={{
                display: 'flex', alignItems: 'center', gap: 10, padding: '12px',
                marginBottom: 2, cursor: 'pointer', borderRadius: 8,
                background: activeTab === section.id ? 'rgba(91,192,190,0.1)' : 'transparent',
                color: activeTab === section.id ? 'var(--c-phos)' : 'var(--txt-2)',
              }}
            >
              {section.icon}
              <span style={{ fontSize: 12, fontWeight: 600 }}>{section.label}</span>
            </div>
          ))}
        </div>

        <div style={{ padding: '16px', borderTop: '1px solid var(--line-2)', display: 'flex', flexDirection: 'column', gap: 10 }}>
          <button onClick={handleRun} disabled={!selectedId} style={{
            width: '100%', padding: '14px', background: selectedId ? 'var(--c-phos)' : 'var(--line-2)', 
            border: 'none', color: '#000', fontFamily: 'var(--f-disp)', fontSize: 12, fontWeight: 800,
            display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8, 
            cursor: selectedId ? 'pointer' : 'not-allowed', borderRadius: 8
          }}>
            <LucidePlay size={16} /> RUN PRE-FLIGHT
          </button>
        </div>
      </div>

    </div>
  );
}
