# Sequential Gemini Slide Generation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Modify the slide deck generator skill's core rules and logic to enable sequential, single-conversation image generation as the default behavior when using the `baoyu-danger-gemini-web` backend, completely avoiding Gemini Web UI conversation clutter.

**Architecture:** We will update the default rules in the slide-deck skill (`SKILL.md`) under the Batch Generation Policy and Step 7 Generate Images. When the active backend is `baoyu-danger-gemini-web`, the agent will execute the image generation shell commands sequentially (from Slide 1 to Slide N) rather than in parallel, appending the same `--sessionId slides-{topic-slug}-{timestamp}` parameter to every single run. A 3-page PPT test workflow will be defined to verify the update.

**Tech Stack:** Bash/CLI, Bun runtime, YAML, Markdown

---

### Task 1: Update Slide Deck Generator Core Rules

**Files:**
- Modify: `/Users/marine/.gemini/config/plugins/baoyu-skills-custom/skills/baoyu-slide-deck/SKILL.md`

- [ ] **Step 1: Modify the Batch Generation Policy in SKILL.md**

Update the rules starting at line 52 to make sequential single-conversation generation the default for the `baoyu-danger-gemini-web` backend:

```diff
 ## Batch Generation Policy
 
 After every prompt file for the current generation group has been saved and verified, generate slide images in batches by default.
 
+**Special Case for `baoyu-danger-gemini-web`**:
+When the image backend is resolved to `baoyu-danger-gemini-web`, ALWAYS generate sequentially in a single conversation thread using the generated `sessionId` (e.g. `slides-{topic-slug}-{timestamp}`). Never generate in parallel by default, as concurrent calls will corrupt the session state and clutter the Gemini UI with separate conversations.
+
 Priority order:
 
-1. Use the chosen backend's native batch / multi-task interface if it exists. Each task must keep its own prompt file, output path, aspect ratio, session ID, and direct reference images.
-2. If no native batch interface exists but the runtime can issue parallel tool calls, dispatch up to `generation_batch_size` slide images at a time. Default: `4`. An explicit user request in the current message, such as `--batch-size 4` or "并行4张一起生成", overrides EXTEND.md.
-3. If neither native batch nor parallel tool calls are available, generate sequentially.
+1. When using `baoyu-danger-gemini-web`, generate sequentially using the same `sessionId` to keep all slides within a single clean conversation.
+2. Otherwise, use the chosen backend's native batch / multi-task interface if it exists. Each task must keep its own prompt file, output path, aspect ratio, session ID, and direct reference images.
+3. If no native batch interface exists but the runtime can issue parallel tool calls, dispatch up to `generation_batch_size` slide images at a time. Default: `4`. An explicit user request in the current message, such as `--batch-size 4` or "并行4张一起生成", overrides EXTEND.md.
+4. If neither native batch nor parallel tool calls are available, generate sequentially.
```

- [ ] **Step 2: Modify the Step 7: Generate Images rule in SKILL.md**

Update the rule at line 304 to enforce sequential execution:

```diff
 299: ### Step 7: Generate Images
 300: 
 301: 1. Resolve the image backend via the Image Generation Tools rule at the top — ask once if multiple are installed.
 302:    - **`codex-imagegen` invocation**: when the rule resolves to `codex-imagegen`, see [references/codex-imagegen.md](references/codex-imagegen.md) for the invocation contract (preferred `baoyu-image-gen --provider codex-cli` path, runtime wrapper discovery, parameter notes, stdout schema, batch semantics — n=1 per call so slide batches must dispatch one wrapper call per slide).
 303: 2. Confirm every `prompts/NN-slide-{slug}.md` exists (hard requirement; prompt files are the reproducibility record regardless of backend).
-304: 3. Session ID: `slides-{topic-slug}-{timestamp}` — pass to the backend only if it supports sessions.
-305: 4. Build a task list for selected slides with each slide's prompt file, output PNG path, aspect ratio, session ID, and verified direct references.
-306: 5. Dispatch slide images in batches per the `## Batch Generation Policy`: backend native batch first, runtime parallel tool calls second, sequential only as fallback. Backup rule applies to PNG files before dispatch. Report progress as `Generated X/N`. Retry only failed items once before reporting an error.
+304: 3. Session ID: Always generate a unique `sessionId` = `slides-{topic-slug}-{timestamp}` for the current run.
+305: 4. Build a task list for selected slides with each slide's prompt file, output PNG path, aspect ratio, session ID, and verified direct references.
+306: 5. Dispatch slide images sequentially (from `01` to `NN`) using the generated `sessionId` (e.g. appending `--sessionId {sessionId}` to each run) when using `baoyu-danger-gemini-web`. Report progress page-by-page as `Generated X/N`.
```

- [ ] **Step 3: Commit rule changes**

```bash
git add /Users/marine/.gemini/config/plugins/baoyu-skills-custom/skills/baoyu-slide-deck/SKILL.md
git commit -m "feat: make sequential single-conversation generation the default for gemini-web backend"
```

---

### Task 2: Verification and Acceptance Testing

We will run a 3-page slide deck generation to verify that the sequential, single-conversation execution flow runs flawlessly.

- [ ] **Step 1: Execute test 3-page PPT generation**

Run the following command to generate a 3-page slide deck for a test topic "系统测试验证":

```bash
/baoyu-slide-deck --slides 3 "直接生成" "测试系统更新是否完毕，验证单会话顺序生成能力是否验收通过"
```

- [ ] **Step 2: Check generated directory structure and files**

Run: `ls -la "/Users/marine/Code/MASS-L3-Tactical Layer/slide-deck/test-system-update/"`
Expected: The directory `test-system-update/` exists and contains:
- `source-test-system-update.md`
- `analysis.md`
- `outline.md`
- `01-slide-cover.png`
- `02-slide-*.png`
- `03-slide-back-cover.png`
- `test-system-update.pptx`
- `test-system-update.pdf`

- [ ] **Step 3: Verify the session file contents on disk**

Find the generated session JSON file in `~/Library/Application Support/baoyu-skills/gemini-web/sessions/` that matches `slides-test-system-update-*.json`.
Verify that:
- The session file contains a valid `id`, a `metadata` array with exactly 3 elements (`[conversationId, responseId, choiceId]`).
- The `messages` array contains the sequential history of the 3 prompts sent during the run.
- Log into the Gemini Web UI and verify that exactly **one** new conversation thread was created, and its title corresponds to the cover slide topic.
