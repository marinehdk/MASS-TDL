# Design Spec: Single-Conversation Sequential Slide Image Generation

## Goal

Currently, the slide deck generator `baoyu-slide-deck` creates slide images in parallel (using `Promise.all` or background tasks). When using the `baoyu-danger-gemini-web` backend (which reverse-engineers the Gemini Web API), this concurrency creates a new conversation in the Gemini Web interface for every single slide. For a 20-page presentation, this clutters the Gemini Web conversation list with 20 separate new threads.

This design enables **sequential, single-conversation generation** as the default behavior. It ensures all slide images for a single PPT presentation are generated under **one single conversation** in Gemini Web, with the conversation title matching the slide deck's topic.

## Proposed Changes

### 1. Unified Session ID Generation

During **Step 1: Setup & Analyze**, the Slide Deck Generator will generate a unique, readable `sessionId` for the slide deck task:
* **Format**: `slides-{topic-slug}-{timestamp}`
* **Example**: `slides-tdl-architecture-workflow-202605291658`
* **Purpose**: Passed to the `baoyu-danger-gemini-web` backend CLI via `--sessionId` to ensure all runs are grouped into the same Gemini conversation thread.

### 2. Sequential CLI Execution (Step 7: Generate Images)

We will modify the image generation loop in **Step 7: Generate Images**:
1. Disable parallel execution (ignore `generation_batch_size` in the default single-conversation mode).
2. Sequentially loop through slides from `01` to `NN`.
3. For each slide, invoke the image generator backend CLI with the `--sessionId` option appended:
   ```bash
   bun /path/to/baoyu-danger-gemini-web/scripts/main.ts --promptfiles prompts/NN-slide-slug.md --image NN-slide-slug.png --sessionId slides-{topic-slug}-{timestamp}
   ```
4. Since `baoyu-danger-gemini-web` automatically saves the conversation state (`conversationId`, `responseId`, `choiceId`) to `sessions/slides-{topic-slug}-{timestamp}.json` after each run, the next command in the loop will read this file and seamlessly reply in the same thread.
5. Provide real-time sequential progress logs to the user (e.g. `[1/20] Generated Slide 1 successfully...`).

### 3. Automatic Conversation Naming

In Gemini Web, the conversation title is automatically generated based on the first prompt sent in the thread. Since the first prompt sent in our thread is Slide 1 (the cover slide), which includes:
```markdown
## SLIDE CONTENT
Headline: [战术决策系统架构与避碰流程汇报]
```
Gemini Web will naturally assign the slide deck's title as the conversation's title. This perfectly fulfills the requirement of matching the conversation name to the PPT topic.

## Verification Plan

### Automated/Manual Verification
- Generate a test 3-page slide deck using the new sequential method.
- Verify that `prompts/` and `.png` output files are created sequentially.
- Verify that a single session JSON file exists in `sessions/slides-{topic-slug}-{timestamp}.json`.
- Log into the Gemini Web UI and verify that exactly **one** new conversation was created, and its title matches the topic of the first slide.
