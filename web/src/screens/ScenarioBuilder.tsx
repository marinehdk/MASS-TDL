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
import { Stepper } from './shared/Stepper';
import { SummaryRail } from './shared/SummaryRail';
import { ImazuGrid } from './shared/ImazuGrid';

const IMAZU_CASES = [
  { id: 1, name: 'Head-on', rule: 'R14', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }] },
  { id: 2, name: 'Overtaking', rule: 'R13', ships: [{ x: 50, y: 30, h: 0, role: 'stand-on' }] },
  { id: 3, name: 'Crossing-give-way', rule: 'R15', ships: [{ x: 78, y: 30, h: 240, role: 'give-way' }] },
  { id: 4, name: 'Crossing-stand-on', rule: 'R17', ships: [{ x: 22, y: 30, h: 120, role: 'stand-on' }] },
  { id: 5, name: 'Crossing-fine', rule: 'R15', ships: [{ x: 70, y: 18, h: 220, role: 'give-way' }] },
  { id: 6, name: 'Crossing-broad', rule: 'R15', ships: [{ x: 85, y: 50, h: 260, role: 'give-way' }] },
  { id: 7, name: 'Overtaken', rule: 'R13', ships: [{ x: 50, y: 95, h: 0, role: 'give-way' }] },
  { id: 8, name: 'HO + OT-port', rule: 'R14+R13', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 28, y: 40, h: 0, role: 'stand-on' }] },
  { id: 9, name: 'HO + CR-stbd', rule: 'R14+R15', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 80, y: 35, h: 240, role: 'give-way' }] },
  { id: 10, name: 'HO + CR-port', rule: 'R14+R17', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 20, y: 35, h: 120, role: 'stand-on' }] },
  { id: 11, name: 'CR-stbd + OT', rule: 'R15+R13', ships: [{ x: 78, y: 30, h: 240, role: 'give-way' }, { x: 50, y: 40, h: 0, role: 'stand-on' }] },
  { id: 12, name: 'CR-port + OT', rule: 'R17+R13', ships: [{ x: 22, y: 30, h: 120, role: 'stand-on' }, { x: 50, y: 40, h: 0, role: 'stand-on' }] },
  { id: 13, name: 'HO + CR-stbd + OT', rule: 'R14+R15+R13', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 80, y: 35, h: 240, role: 'give-way' }, { x: 50, y: 45, h: 0, role: 'stand-on' }] },
  { id: 14, name: 'HO + CR-port + OT', rule: 'R14+R17+R13', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 20, y: 35, h: 120, role: 'stand-on' }, { x: 50, y: 45, h: 0, role: 'stand-on' }] },
  { id: 15, name: 'CR-stbd x2', rule: 'R15x2', ships: [{ x: 78, y: 25, h: 240, role: 'give-way' }, { x: 88, y: 50, h: 260, role: 'give-way' }] },
  { id: 16, name: 'CR-port x2', rule: 'R17x2', ships: [{ x: 22, y: 25, h: 120, role: 'stand-on' }, { x: 12, y: 50, h: 100, role: 'stand-on' }] },
  { id: 17, name: 'CR-stbd + CR-port', rule: 'R15+R17', ships: [{ x: 78, y: 30, h: 240, role: 'give-way' }, { x: 22, y: 30, h: 120, role: 'stand-on' }] },
  { id: 18, name: 'HO + 2x CR-stbd', rule: 'R14+R15x2', ships: [{ x: 50, y: 20, h: 180, role: 'give-way' }, { x: 75, y: 30, h: 230, role: 'give-way' }, { x: 90, y: 50, h: 255, role: 'give-way' }] },
  { id: 19, name: 'OT + CR-stbd + CR-port', rule: 'R13+R15+R17', ships: [{ x: 50, y: 35, h: 0, role: 'stand-on' }, { x: 78, y: 30, h: 240, role: 'give-way' }, { x: 22, y: 30, h: 120, role: 'stand-on' }] },
  { id: 20, name: 'HO + CR + CR', rule: 'R14+R15+R17', ships: [{ x: 50, y: 18, h: 180, role: 'give-way' }, { x: 78, y: 40, h: 240, role: 'give-way' }, { x: 22, y: 40, h: 120, role: 'stand-on' }] },
  { id: 21, name: 'Dense traffic (4)', rule: 'complex', ships: [{ x: 50, y: 22, h: 180, role: 'give-way' }, { x: 75, y: 30, h: 230, role: 'give-way' }, { x: 25, y: 35, h: 130, role: 'stand-on' }, { x: 50, y: 50, h: 0, role: 'stand-on' }] },
  { id: 22, name: 'Restricted waters', rule: 'R9 complex', ships: [{ x: 50, y: 25, h: 180, role: 'give-way' }, { x: 70, y: 40, h: 220, role: 'give-way' }] },
];

export function ScenarioBuilder() {
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [yamlEditor, setYamlEditor] = useState('');
  const [activeTab, setActiveTab] = useState<'template' | 'procedural' | 'ais'>('template');
  const [currentStep, setCurrentStep] = useState<1 | 2 | 3>(1);
  const [imazuId, setImazuId] = useState<number | null>(3);
  const [validateBtn, setValidateBtn] = useState<'red' | 'yellow' | 'green'>('yellow');

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
    <div style={{ display: 'grid', gridTemplateColumns: '220px 1fr 280px', height: '100%' }}>
      {/* LEFT: Step sidebar + Scenario Library */}
      <div style={{
        background: 'var(--bg-1)', borderRight: '1px solid var(--line-2)',
        padding: '16px 0', display: 'flex', flexDirection: 'column',
      }}>
        <div style={{
          fontFamily: 'var(--f-disp)', fontSize: 9.5, color: 'var(--txt-3)',
          letterSpacing: '0.22em', textTransform: 'uppercase',
          padding: '0 16px', marginBottom: 8,
        }}>SCENARIO STEPS</div>

        {[
          { id: 1, code: 'A', label: 'ENCOUNTER', zh: '会遇几何 · Imazu 22' },
          { id: 2, code: 'B', label: 'ENVIRONMENT', zh: '海况 · 传感器 · 本船' },
          { id: 3, code: 'C', label: 'RUN', zh: '时序 · 故障 · 评估' },
        ].map((step) => {
          const active = currentStep === step.id;
          return (
            <div key={step.id} onClick={() => setCurrentStep(step.id as 1 | 2 | 3)} style={{
              padding: '12px 16px', cursor: 'pointer',
              borderLeft: active ? '3px solid var(--c-phos)' : '3px solid transparent',
              background: active ? 'var(--bg-0)' : 'transparent',
              display: 'flex', alignItems: 'center', gap: 12,
            }}>
              <div style={{
                width: 22, height: 22,
                border: `1px solid ${active ? 'var(--c-phos)' : 'var(--line-2)'}`,
                color: active ? 'var(--c-phos)' : 'var(--txt-3)',
                display: 'flex', alignItems: 'center', justifyContent: 'center',
                fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 700,
              }}>{step.code}</div>
              <div>
                <div style={{
                  fontFamily: 'var(--f-disp)', fontSize: 11.5,
                  color: active ? 'var(--txt-0)' : 'var(--txt-1)',
                  fontWeight: 600, letterSpacing: '0.14em', textTransform: 'uppercase',
                }}>{step.label}</div>
                <div style={{
                  fontFamily: 'var(--f-body)', fontSize: 10, color: 'var(--txt-3)', marginTop: 1,
                }}>{step.zh}</div>
              </div>
            </div>
          );
        })}

        <div style={{ flex: 1 }} />

        {/* Scenario Library presets */}
        <div style={{ padding: '0 16px' }}>
          <div style={{
            fontFamily: 'var(--f-disp)', fontSize: 9.5, color: 'var(--txt-3)',
            letterSpacing: '0.22em', textTransform: 'uppercase', marginBottom: 6,
          }}>SCENARIO LIBRARY</div>
          {['IM03_Crossing_GiveWay_v2', 'IM07_Overtaken_FogA', 'IM14_Triple_Conflict', 'IM22_Restricted_Narrow'].map((name, i) => (
            <div key={i} style={{
              padding: '5px 8px', marginBottom: 3,
              background: i === 0 ? 'var(--bg-0)' : 'transparent',
              borderLeft: i === 0 ? '2px solid var(--c-phos)' : '2px solid transparent',
            }}>
              <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: i === 0 ? 'var(--txt-0)' : 'var(--txt-2)' }}>
                {name}
              </span>
            </div>
          ))}
        </div>
      </div>

      {/* CENTER: Step content */}
      <div style={{ padding: 16, overflow: 'hidden' }}>
        {currentStep === 1 && (
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 340px', gap: 14, height: '100%', overflow: 'hidden' }}>
            <div style={{ overflowY: 'auto', paddingRight: 8 }}>
              <ImazuGrid
                cases={IMAZU_CASES}
                selected={imazuId}
                onSelect={(id) => { setImazuId(id); }}
              />
            </div>
            {(() => {
              const sel = IMAZU_CASES.find(c => c.id === imazuId) || IMAZU_CASES[2];
              return (
                <div style={{ display: 'flex', flexDirection: 'column', gap: 10, overflowY: 'auto' }}>
                  {/* Detail card */}
                  <div style={{
                    background: 'var(--bg-1)', border: '1px solid var(--line-2)',
                    borderLeft: '2px solid var(--c-phos)', padding: 10,
                  }}>
                    <div style={{
                      fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
                      letterSpacing: '0.18em', textTransform: 'uppercase', marginBottom: 6,
                    }}>选中场景 · 详情</div>
                    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
                      <span style={{ fontFamily: 'var(--f-disp)', fontSize: 16, color: 'var(--txt-0)', fontWeight: 700, letterSpacing: '0.08em' }}>
                        IM{String(sel.id).padStart(2, '0')} · {sel.name}
                      </span>
                      <span style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--c-warn)', fontWeight: 600 }}>
                        {sel.rule}
                      </span>
                    </div>
                    <div style={{ fontFamily: 'var(--f-mono)', fontSize: 9.5, color: 'var(--txt-2)', marginTop: 4 }}>
                      Targets = {sel.ships.length} · Give-way {sel.ships.filter(s => s.role === 'give-way').length} · Stand-on {sel.ships.filter(s => s.role === 'stand-on').length}
                    </div>
                    {/* Radar SVG */}
                    <div style={{
                      marginTop: 8, background: '#050810', border: '1px solid var(--line-1)',
                      padding: 6,
                    }}>
                      <svg viewBox="0 0 100 100" style={{ width: '100%', height: 180 }}>
                        <circle cx="50" cy="75" r="28" fill="none" stroke="var(--line-2)" strokeWidth="0.5" strokeDasharray="2 3" />
                        <circle cx="50" cy="75" r="56" fill="none" stroke="var(--line-2)" strokeWidth="0.5" strokeDasharray="2 3" opacity="0.6" />
                        <line x1="50" y1="75" x2="50" y2="15" stroke="var(--c-phos)" strokeWidth="0.6" opacity="0.5" />
                        <text x="47" y="12" fontFamily="var(--f-mono)" fontSize="4" fill="var(--c-phos)">N</text>
                        {sel.ships.map((s, i) => {
                          const c = s.role === 'give-way' ? 'var(--c-danger)' : s.role === 'stand-on' ? 'var(--c-warn)' : 'var(--c-info)';
                          const rad = (s.h - 90) * Math.PI / 180;
                          return (
                            <g key={i}>
                              <line x1={s.x} y1={s.y} x2={s.x + Math.cos(rad) * 12} y2={s.y + Math.sin(rad) * 12} stroke={c} strokeWidth="0.8" />
                              <g transform={`translate(${s.x},${s.y}) rotate(${s.h})`}>
                                <path d="M 0 -3.5 L 2.5 2.5 L 0 1 L -2.5 2.5 Z" fill="none" stroke={c} strokeWidth="1" />
                              </g>
                              <text x={s.x + 4} y={s.y - 2} fontFamily="var(--f-disp)" fontSize="3.5" fill={c}>T0{i + 1}</text>
                            </g>
                          );
                        })}
                        <g transform="translate(50,75)">
                          <path d="M 0 -3.5 L 2.5 2.5 L 0 1 L -2.5 2.5 Z" fill="var(--c-phos)" />
                        </g>
                        <text x="53" y="78" fontFamily="var(--f-disp)" fontSize="3.5" fill="var(--c-phos)">OWN</text>
                      </svg>
                    </div>
                    {/* Target chips */}
                    <div style={{ marginTop: 8, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 4 }}>
                      {sel.ships.map((s, i) => {
                        const c = s.role === 'give-way' ? 'var(--c-danger)' : s.role === 'stand-on' ? 'var(--c-warn)' : 'var(--c-info)';
                        return (
                          <div key={i} style={{
                            padding: '5px 7px', background: 'var(--bg-0)',
                            borderLeft: `2px solid ${c}`,
                          }}>
                            <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9.5, color: 'var(--txt-0)', fontWeight: 600, letterSpacing: '0.08em' }}>
                              T0{i + 1}
                            </span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: c, display: 'block', textTransform: 'uppercase' }}>
                              {s.role}
                            </span>
                            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 8, color: 'var(--txt-3)' }}>
                              HDG {s.h}° · pos ({s.x}, {s.y})
                            </span>
                          </div>
                        );
                      })}
                    </div>
                  </div>
                  {/* Parameter tuning */}
                  <div style={{
                    background: 'var(--bg-1)', border: '1px solid var(--line-1)',
                    borderLeft: '2px solid var(--line-3)', padding: 10,
                  }}>
                    <div style={{
                      fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
                      letterSpacing: '0.18em', textTransform: 'uppercase', marginBottom: 6,
                    }}>可选 · 目标参数微调</div>
                    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
                      <div>
                        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 7.5, color: 'var(--txt-3)', textTransform: 'uppercase' }}>目标船速范围</span>
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-1)', marginTop: 2 }}>10.0 – 18.0 kn</div>
                      </div>
                      <div>
                        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 7.5, color: 'var(--txt-3)', textTransform: 'uppercase' }}>初始距离</span>
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-1)', marginTop: 2 }}>4.5 – 6.0 nm</div>
                      </div>
                      <div>
                        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 7.5, color: 'var(--txt-3)', textTransform: 'uppercase' }}>意图扰动</span>
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--c-warn)', marginTop: 2 }}>σ = 4° · σ_SOG = 0.8 kn</div>
                      </div>
                      <div>
                        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 7.5, color: 'var(--txt-3)', textTransform: 'uppercase' }}>意图突变事件</span>
                        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-1)', marginTop: 2 }}>1 × @ T+180s</div>
                      </div>
                    </div>
                  </div>
                </div>
              );
            })()}
          </div>
        )}
        {currentStep === 2 && (
          <div style={{ color: 'var(--txt-3)', padding: 20 }}>
            <h3 style={{ fontFamily: 'var(--f-disp)', color: 'var(--txt-1)', textTransform: 'uppercase' }}>
              ENVIRONMENT — 海况 · 传感器 · 本船
            </h3>
            <p>Environment configuration (Phase 2: full SeaState/Sensor/Hull/ODD cards per spec §3.1.2)</p>
            {selectedId && scenarios.length > 0 && (
              <p style={{ color: 'var(--c-phos)' }}>
                Scenario: {scenarios.find((s: any) => s.id === selectedId)?.name || selectedId}
              </p>
            )}
          </div>
        )}
        {currentStep === 3 && (
          <div style={{ color: 'var(--txt-3)', padding: 20 }}>
            <h3 style={{ fontFamily: 'var(--f-disp)', color: 'var(--txt-1)', textTransform: 'uppercase' }}>
              RUN — 时序 · 故障 · 评估
            </h3>
            <p>Run configuration (Phase 2: full Timing/IvP Weights/Fault Script/Pass Criteria per spec §3.1.3)</p>
            <button onClick={handleRun} style={{
              marginTop: 16, background: 'var(--c-phos)', border: 'none', color: 'var(--bg-0)',
              padding: '12px 24px', fontFamily: 'var(--f-disp)', fontSize: 11,
              letterSpacing: '0.18em', fontWeight: 700, cursor: 'pointer',
            }}>VALIDATE & PRE-FLIGHT →</button>
          </div>
        )}
      </div>

      {/* RIGHT: SummaryRail */}
      <SummaryRail
        summary={{
          name: scenarios.find((s: any) => s.id === selectedId)?.name || 'Select scenario',
          imazuId: imazuId ?? 1,
          targets: IMAZU_CASES.find(c => c.id === imazuId)?.ships.length ?? 0,
          duration: 600,
          seed: 20260513,
          odd: 'A · OPEN',
          daypart: 'NIGHT',
        }}
        validationStatus={validateBtn}
        onValidate={handleValidate}
        onRunPreflight={handleRun}
      />
    </div>
  );
}
