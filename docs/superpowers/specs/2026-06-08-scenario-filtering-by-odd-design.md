# Design Specification — Scenario List Filtering by ODD Domain

This specification details the design for dynamically filtering the Scenario Builder scenario library list based on the active ODD (Operational Domain) selection.

## Context & Problem Statement

Currently, the Scenario Builder displays all YAML scenarios in the "场景库" (Scenario Library) sidebar tab, regardless of which ODD region (Norway vs. Malacca Strait) is selected in the "运行域" tab. Because Norway and Malacca Strait use coordinates that are thousands of miles apart, loading a Norway scenario while the map is set to Malacca causes vessels to render outside chart bounds, preventing water depth retrieval and making the simulation physically incorrect.

To enforce physical and ODD constraints, we will filter the scenario list so that:
- Selecting **"近海群岛"** (`coastal_archipelago` / Malacca Strait) only displays scenarios located in the Malacca coordinates (latitude < 10.0°).
- Selecting **"港口水域"** (`harbour_approach` / Norway) and other Norway domains only displays scenarios located in Norway coordinates (latitude >= 10.0°).

## Proposed Architecture & Changes

### 1. Backend: Metadata Extraction on List API

Modify `ScenarioStore.list()` in `src/sil_orchestrator/scenario_store.py` to:
- Open and parse each YAML file to extract own ship position and domain metadata.
- Extract:
  - `latitude`: from `ownShip.initial.position.latitude` (default to `63.44` if missing or malformed).
  - `longitude`: from `ownShip.initial.position.longitude` (default to `10.38` if missing or malformed).
  - `odd_domain`: from `metadata.odd_cell.domain` (default to `'harbour_approach'` if missing).
- Add `latitude`, `longitude`, and `odd_domain` to each scenario's returned dictionary in the list.

### 2. Frontend API Types

Modify `ScenarioSummary` in `web/src/api/silApi.ts` to include:
- `latitude?: number;`
- `longitude?: number;`
- `odd_domain?: string;`

### 3. Frontend: Dynamic Scenario List Filtering

Modify `web/src/screens/SimulationScenario.tsx`:
- Default `activeLeftTab` to `'odd'` (Running Domain) on initial mount.
- Update the scenario list filtering memo `filteredSuites`:
  - If `oddDomain === 'coastal_archipelago'` (近海群岛):
    - Filter scenarios to only show those with Malacca coordinates (`latitude < 10.0` or `odd_domain === 'coastal_archipelago'`).
  - Otherwise:
    - Filter scenarios to only show those with Norway coordinates (`latitude >= 10.0` or `odd_domain !== 'coastal_archipelago'`).

## Verification Plan

### Automated Verification
- Verify Vitest frontend unit tests pass.
- Write a Playwright E2E test `web/e2e/scenario-filtering.spec.ts` verifying:
  - Default tab is "运行域".
  - When "近海群岛" is selected, the scenario library tab (once opened) only contains Malacca Strait scenarios (or is empty if none exist yet).
  - When "港口水域" is selected, the scenario library contains Norway scenarios (`colreg-rule14-ho`, etc.).
