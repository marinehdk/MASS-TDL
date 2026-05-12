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
import type { ScenarioSummary } from '../api/silApi';

export function ScenarioBuilder() {
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [yamlEditor, setYamlEditor] = useState('');
  const [activeTab, setActiveTab] = useState<'template' | 'procedural' | 'ais'>('template');

  const { data: scenarios = [] } = useListScenariosQuery();
  const { data: scenarioDetail } = useGetScenarioQuery(selectedId!, { skip: !selectedId });
  const [validate, { data: validation }] = useValidateScenarioMutation();
  const [createScenario] = useCreateScenarioMutation();
  const [deleteScenario] = useDeleteScenarioMutation();
  const setScenario = useScenarioStore((s) => s.setScenario);

  const handleSelect = (id: string) => {
    setSelectedId(id);
  };

  useEffect(() => {
    if (scenarioDetail && selectedId) {
      setYamlEditor(scenarioDetail.yaml_content);
    }
  }, [scenarioDetail, selectedId]);

  const handleValidate = () => validate(yamlEditor);

  const handleSave = async () => {
    const result = await createScenario(yamlEditor).unwrap();
    setScenario(result.scenario_id, result.hash);
  };

  const handleRun = () => {
    if (selectedId) {
      const hash = scenarioDetail?.hash || '';
      setScenario(selectedId, hash);
      window.location.hash = `#/preflight/${selectedId}`;
    }
  };

  return (
    <div style={{ display: 'flex', height: '100vh' }} data-testid="scenario-builder">
      {/* Left panel: form */}
      <div style={{ width: 400, padding: 16, overflowY: 'auto', borderRight: '1px solid #333' }}>
        <h2>Scenario Builder</h2>

        {/* Tab bar */}
        <div style={{ display: 'flex', gap: 8, marginBottom: 16 }}>
          {(['template', 'procedural', 'ais'] as const).map((tab) => (
            <button key={tab} onClick={() => setActiveTab(tab)}
                    style={{ fontWeight: activeTab === tab ? 'bold' : 'normal' }}>
              {tab === 'template' ? 'Template' : tab === 'procedural' ? 'Procedural' : 'AIS'}
            </button>
          ))}
        </div>

        {/* Scenario list */}
        <select size={8} style={{ width: '100%', marginBottom: 8 }}
                onChange={(e) => handleSelect(e.target.value)}>
          {scenarios.map((s: ScenarioSummary) => (
            <option key={s.id} value={s.id}>{s.name} ({s.encounter_type})</option>
          ))}
        </select>

        {/* YAML editor */}
        <textarea
          value={yamlEditor}
          onChange={(e) => setYamlEditor(e.target.value)}
          placeholder="Paste YAML or select scenario..."
          rows={20}
          style={{ width: '100%', fontFamily: 'monospace', fontSize: 12 }}
          data-testid="yaml-editor"
        />

        {/* Action buttons */}
        <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
          <button onClick={handleValidate} data-testid="btn-validate">Validate</button>
          <button onClick={handleSave} data-testid="btn-save">Save</button>
          <button onClick={() => selectedId && deleteScenario(selectedId)}
                  data-testid="btn-delete" disabled={!selectedId}>Delete</button>
          <button onClick={handleRun} data-testid="btn-run"
                  disabled={!selectedId} style={{ marginLeft: 'auto', background: '#2dd4bf' }}>
            Run &rarr;
          </button>
        </div>

        {validation && (
          <div style={{ marginTop: 8, color: validation.valid ? 'green' : 'red' }}>
            {validation.valid ? 'Valid' : validation.errors.join(', ')}
          </div>
        )}
      </div>

      {/* Right panel: ENC + Imazu preview */}
      <div style={{ flex: 1 }}>
        <SilMapView />
      </div>
    </div>
  );
}
