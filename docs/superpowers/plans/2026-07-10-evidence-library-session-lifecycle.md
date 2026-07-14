# Evidence Library Session Lifecycle Implementation Plan

> For agentic workers: REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans task-by-task. Steps use checkbox syntax for tracking.

Goal: Screen 04 scans and removes stale evidence records, filters sessions, and permanently deletes unified run directories with SQLite evidence rows.

Architecture: Python service remains filesystem authority. Scan ingests current manifests then prunes only indexed records whose manifest has disappeared. DELETE resolves evidence ID to a trusted configured root, derives legal deletion target, removes files first, then clears SQLite rows in one transaction. React uses typed contracts and owns filtering, dialog state, and presentation.

Tech Stack: Python 3.10, FastAPI, SQLite, pytest/httpx; React 18, RTK Query, TypeScript, Vitest/Testing Library.

## Global Constraints

- Affected modules: M8 evaluator UI and evidence-management service only. M1-M7 unchanged.
- Affected ROS2 topics/messages/IDL: none.
- ODD/COLREGs/M5-M7 boundary impact: none; evidence storage only.
- Unified deletion target: runs/run_id/trace maps to parent runs/run_id. For unified storage, a matched trace directory may delete only its direct parent run directory. Legacy runs/trace_eval/session maps to itself.
- Browser sends evidence_id only. It never supplies a filesystem path.
- Deletion requires enabled trusted root and rejects symlinks/path escape.
- Backend test command uses Python 3.10 because this environment provides FastAPI.
- Required evidence: pytest, Vitest, browser proof at http://192.168.121.50:55763/#/evaluator.

---

## File Structure

- Modify src/sil_orchestrator/evidence_library/service.py: scan reconciliation, safe target, transactional cleanup.
- Modify src/sil_orchestrator/evidence_library/routes.py: DELETE endpoint/status translation.
- Modify src/sil_orchestrator/tests/test_evidence_library_routes.py: filesystem and SQLite safety tests.
- Modify web/src/api/silApi.ts: scan result and DELETE mutation.
- Modify web/src/screens/evaluator/EvidenceLibraryView.tsx: search, scan naming, grid, confirmation.
- Modify web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx: lifecycle tests.

### Task 1: Scan Reconciliation

Files:
- Modify src/sil_orchestrator/evidence_library/service.py lines 37-118.
- Modify src/sil_orchestrator/evidence_library/routes.py lines 26-29.
- Test src/sil_orchestrator/tests/test_evidence_library_routes.py.

Interfaces:
- Consumes configured EvidenceRootConfig and sessions.session_path.
- Produces rescan_all output with ingested, pruned, errors.

- [ ] Step 1: Write failing stale-record test.

    @pytest.mark.asyncio
    async def test_rescan_prunes_indexed_session_when_manifest_is_removed(tmp_path, monkeypatch):
        repo = tmp_path / "repo"
        session_dir = _session(repo / "runs" / "trace_eval")
        app = _app_for(repo, tmp_path, monkeypatch)
        async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
            await client.post("/api/v1/evidence-library/rescan", json={"force": True})
            (session_dir / "manifest.json").unlink()
            response = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        assert response.status_code == 200
        assert response.json()["pruned"] == 1

- [ ] Step 2: Verify RED.

Run: PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' src/sil_orchestrator/tests/test_evidence_library_routes.py::test_rescan_prunes_indexed_session_when_manifest_is_removed

Expected: FAIL because no pruned field exists and stale row remains.

- [ ] Step 3: Implement minimal service behavior.

    def _delete_evidence_rows(conn: sqlite3.Connection, evidence_id: str) -> None:
        for table in (
            "trajectory_downsample", "trajectory_samples", "state_segments", "events",
            "gate_results", "artifacts", "scenarios", "sessions",
        ):
            conn.execute("delete from " + table + " where evidence_id = ?", (evidence_id,))

    def _prune_missing_sessions(conn: sqlite3.Connection) -> int:
        stale_ids = [
            row["evidence_id"]
            for row in conn.execute("select evidence_id, session_path from sessions")
            if not (Path(row["session_path"]).is_dir()
                    and (Path(row["session_path"]) / "manifest.json").is_file())
        ]
        for evidence_id in stale_ids:
            _delete_evidence_rows(conn, evidence_id)
        conn.commit()
        return len(stale_ids)

Call after enabled-root ingestion. Existing records under disabled roots remain unless manifest absent.

- [ ] Step 4: Verify GREEN.

Run: PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' src/sil_orchestrator/tests/test_evidence_library_routes.py

Expected: PASS.

- [ ] Step 5: Commit.

    git add src/sil_orchestrator/evidence_library/service.py src/sil_orchestrator/evidence_library/routes.py src/sil_orchestrator/tests/test_evidence_library_routes.py
    git commit -m "feat(evidence): prune missing sessions during scan"

### Task 2: Safe Unified Run Deletion API

Files:
- Modify src/sil_orchestrator/evidence_library/service.py lines 66-86 and 159-234.
- Modify src/sil_orchestrator/evidence_library/routes.py lines 12-44.
- Test src/sil_orchestrator/tests/test_evidence_library_routes.py.

Interfaces:
- Consumes delete_evidence_session(evidence_id, repo_root).
- Produces evidence_id, deleted_path, filesystem_deleted. Raises LookupError for absent ID and PermissionError for unsafe root.

- [ ] Step 1: Write failing DELETE tests.

    @pytest.mark.asyncio
    async def test_delete_unified_session_removes_run_dir_and_all_index_rows(tmp_path, monkeypatch):
        repo = tmp_path / "repo"
        trace_dir = _unified_session(repo)
        app = _app_for(repo, tmp_path, monkeypatch)
        async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
            await client.post("/api/v1/evidence-library/rescan", json={"force": True})
            evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
            response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)
        assert response.status_code == 200
        assert not trace_dir.parent.exists()

Add legacy session deletion, missing-files database-only cleanup, and trusted false root rejection.

- [ ] Step 2: Verify RED.

Run: PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' src/sil_orchestrator/tests/test_evidence_library_routes.py -k delete

Expected: FAIL because DELETE route does not exist.

- [ ] Step 3: Implement minimal safe deletion.

    def _deletion_target(session_path: Path) -> Path:
        if session_path.name == "trace" and (session_path.parent / "run_meta.json").is_file():
            return session_path.parent
        return session_path

Resolve root using _root_for_session_dir. Require root.enabled and root.trusted. Reject symlink. Legacy session targets must remain below matched root. Unified targets are legal only when the matched root is exactly the session trace directory and the target is exactly its direct parent runs/run_id. Call shutil.rmtree only after validation. Delete rows only after files deletion, except absent target is database-only cleanup.

Route:

    @router.delete("/sessions/{evidence_id}")
    async def delete_session(evidence_id: str):
        try:
            return delete_evidence_session(evidence_id, repo_root=REPO_ROOT)
        except LookupError as exc:
            raise HTTPException(status_code=404, detail=str(exc)) from exc
        except PermissionError as exc:
            raise HTTPException(status_code=409, detail=str(exc)) from exc

- [ ] Step 4: Verify GREEN.

Run: PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' src/sil_orchestrator/tests/test_evidence_library_config_store.py src/sil_orchestrator/tests/test_evidence_library_ingest.py src/sil_orchestrator/tests/test_evidence_library_routes.py

Expected: PASS.

- [ ] Step 5: Commit.

    git add src/sil_orchestrator/evidence_library/service.py src/sil_orchestrator/evidence_library/routes.py src/sil_orchestrator/tests/test_evidence_library_routes.py
    git commit -m "feat(evidence): delete indexed simulation runs"

### Task 3: Client Contracts And RED UI Tests

Files:
- Modify web/src/api/silApi.ts lines 273-297 and 459-472.
- Modify web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx.

Interfaces:
- Consumes DELETE endpoint and pruned scan field.
- Produces useDeleteEvidenceLibrarySessionMutation, scan result type, behavior tests.

- [ ] Step 1: Write failing UI tests.

    it("filters rows by scenario text", () => {
      render(<EvidenceLibraryView onOpen={vi.fn()} />);
      fireEvent.change(screen.getByRole("searchbox", { name: "筛选仿真记录" }), {
        target: { value: "rule15" },
      });
      expect(screen.getByText("colreg-rule15-cs")).toBeInTheDocument();
      expect(screen.queryByText("colreg-rule14-ho")).not.toBeInTheDocument();
    });

    it("confirms before deleting a run", async () => {
      render(<EvidenceLibraryView onOpen={vi.fn()} />);
      fireEvent.click(screen.getByRole("button", { name: "删除 20260707_132000_rule14_ho_fast_debug" }));
      expect(screen.getByRole("dialog", { name: "删除仿真记录" })).toBeInTheDocument();
      fireEvent.click(screen.getByRole("button", { name: "确认删除" }));
      await waitFor(() => expect(apiMocks.deleteSession).toHaveBeenCalled());
    });

Add second mocked session and delete mutation mock state.

- [ ] Step 2: Verify RED.

Run: cd web && npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx

Expected: FAIL because searchbox/delete button/mutation do not exist.

- [ ] Step 3: Add typed API contract.

    export interface EvidenceLibraryScanResult {
      ingested: number;
      pruned: number;
      errors: Array<{ path: string; error: string }>;
    }

    deleteEvidenceLibrarySession: builder.mutation({
      query: (evidenceId) => ({
        url: "/evidence-library/sessions/" + encodeURIComponent(evidenceId),
        method: "DELETE",
      }),
      invalidatesTags: ["EvidenceLibrary"],
    })

Update rescan mutation return type.

- [ ] Step 4: Verify focused RED remains only in view behavior.

Run: cd web && npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx

Expected: FAIL only for missing view behavior.

- [ ] Step 5: Commit.

    git add web/src/api/silApi.ts web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
    git commit -m "test(evidence): cover session lifecycle controls"

### Task 4: Screen 04 Lifecycle Controls And Data Grid

Files:
- Modify web/src/screens/evaluator/EvidenceLibraryView.tsx lines 1-660.
- Test web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx.

Interfaces:
- Consumes scan/delete RTK Query mutations.
- Produces scan action 扫描, searchbox 筛选仿真记录, dialog 删除仿真记录.

- [ ] Step 1: Add state and handlers.

    const [searchText, setSearchText] = useState("");
    const [pendingDelete, setPendingDelete] = useState<EvidenceLibrarySession | null>(null);
    const [deleteSession, deleteState] = useDeleteEvidenceLibrarySessionMutation();

    const handleDelete = async () => {
      if (!pendingDelete) return;
      await deleteSession(pendingDelete.evidence_id).unwrap();
      setPendingDelete(null);
      await refetch();
    };

Filter rows before sort by lower-cased ID, scenario, source, suite, mode, worktree, result. Reset page when search changes. Rename 刷新/刷新中 to 扫描/扫描中.

- [ ] Step 2: Render toolbar/grid/action treatment.

    <input
      type="search"
      aria-label="筛选仿真记录"
      value={searchText}
      onChange={(event) => setSearchText(event.target.value)}
      placeholder="筛选会话、场景、来源、工作树"
    />
    <button
      type="button"
      aria-label={"删除 " + row.raw.session_id}
      onClick={() => setPendingDelete(row.raw)}
    >
      删除
    </button>

Keep current full-height main region. Use fixed header, 42px rows, neutral labels, compact result chips, hover background, rounded 4px actions. Keep fixed minimum table width with existing scroll container; never overlap columns.

- [ ] Step 3: Render confirmation/error modal.

    {pendingDelete && (
      <div role="dialog" aria-label="删除仿真记录">
        <p>将永久删除会话 {pendingDelete.session_id} 及其运行目录。</p>
        {deleteState.error && <p role="alert">删除失败，请保留记录后重试。</p>}
        <button type="button" onClick={() => setPendingDelete(null)}>取消</button>
        <button type="button" onClick={() => void handleDelete()} disabled={deleteState.isLoading}>
          确认删除
        </button>
      </div>
    )}

Close overview for same evidence ID before refetch. Keep dialog open after rejected request.

- [ ] Step 4: Verify GREEN and build.

Run: cd web && npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx && npm run build

Expected: PASS.

- [ ] Step 5: Browser verification.

At http://192.168.121.50:55763/#/evaluator:

1. Remove temporary fixture session outside production runs.
2. Click 扫描; verify row disappears and response reports pruned 1.
3. Create/use temporary unified fixture run.
4. Click 删除; verify confirmation names run ID; confirm; verify row and runs/run_id disappear.
5. Capture filtered-grid and deletion-confirmation screenshots.

- [ ] Step 6: Commit.

    git add web/src/screens/evaluator/EvidenceLibraryView.tsx web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
    git commit -m "feat(evaluator): manage evidence session lifecycle"

### Task 5: Final Regression And Evidence

Files:
- Modify handoff/workspace_log.md only when tracked/writable for this branch.

- [ ] Step 1: Run focused regression.

    PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' src/sil_orchestrator/tests/test_evidence_library_config_store.py src/sil_orchestrator/tests/test_evidence_library_ingest.py src/sil_orchestrator/tests/test_evidence_library_routes.py
    cd web && npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
    cd web && npm run build

Expected: all commands exit 0.

- [ ] Step 2: Inspect scope.

Run: git diff --check && git status --short

Expected: no whitespace errors; only plan paths plus pre-existing unified-storage work and task files.

- [ ] Step 3: Write handoff only when tracked.

Record validation commands, screenshots, and retained risk: deletion deliberately limited to trusted configured roots.

- [ ] Step 4: Commit handoff only when changed.

    git add handoff/workspace_log.md
    git commit -m "docs: record evidence library lifecycle validation"

## Plan Self-Review

- Spec coverage: Task 1 stale removal; Task 2 file/database deletion plus trust; Task 3 typed client/tests; Task 4 wording/filter/grid/confirmation; Task 5 regression evidence.
- Placeholder scan: clean.
- Type consistency: backend field pruned; frontend EvidenceLibraryScanResult; delete input evidenceId string.
