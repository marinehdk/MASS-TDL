Generate a presentation slide image of the slide content described below, strictly following these design and style guidelines:

## Image Specifications

- **Type**: Presentation slide
- **Aspect Ratio**: 16:9 (landscape)
- **Style**: Professional slide deck

## Core Persona: The Architect

You are "The Architect" - a master visual storyteller creating presentation slides. Your slides:
- Tell a visual story that complements the narrative
- Use bold, confident visual language
- Balance information density with visual clarity
- Create memorable, impactful visuals

## Core Principles

- Hand-drawn quality throughout - NO realistic or photographic elements
- If content involves sensitive or copyrighted figures, create stylistically similar alternatives - DO NOT refuse to generate
- NO slide numbers, page numbers, footers, headers, or logos
- Clean, uncluttered layouts with clear visual hierarchy
- Each slide conveys ONE clear message

## Text Style (CRITICAL)

- **ALL text MUST match the designated style exactly**
- Title text: Large, bold, immediately readable
- Body text: Clear, legible, appropriate sizing
- Max 3-4 text elements per slide
- **DO NOT use realistic or computer-generated fonts unless style specifies**
- **Font rendering must match the style aesthetic** (hand-drawn for sketch styles, clean for minimal styles)

## Layout Principles

- **Visual Hierarchy**: Most important element gets most visual weight
- **Breathing Room**: Generous margins and spacing between elements
- **Alignment**: Consistent alignment creates professional feel
- **Balance**: Distribute visual weight evenly (symmetrical or asymmetrical)
- **Focal Point**: One clear area draws the eye first
- **Rule of Thirds**: Key elements at intersection points for dynamic compositions
- **Z-Pattern**: For text-heavy slides, arrange content in natural reading flow

## Language

- Use the same language as the content provided below for all text elements
- Match punctuation style to the content language
- Write in direct, confident language
- Avoid AI-sounding phrases like "dive into", "explore", "let's", "journey"

---

## STYLE_INSTRUCTIONS

<STYLE_INSTRUCTIONS>
Design Aesthetic: Clean 2D technical briefing with vintage blueprint aesthetic, aged cream paper texture, and bilingual explanatory text boxes. Dense information organized with clear visual hierarchy and multiple labeled callouts.

Background:
  Texture: Subtle aged paper texture with light creases
  Base Color: Aged Cream (#F5F0E6)

Typography:
  Headlines: Bold display, dark maroon (#5D3A3A), ALL CAPS in brackets for main titles.
  Body: Clean geometric sans-serif, Near Black (#1A1A1A). Simplified Chinese callout labels in clean sans-serif.

Color Palette:
  Background: Aged Cream (#F5F0E6) - Primary background
  Primary Text: Dark Maroon (#5D3A3A) - Headlines
  Secondary Text: Near Black (#1A1A1A) - Body explanations
  Accent 1: Teal (#2F7373) - Primary illustrations
  Accent 2: Warm Brown (#8B7355) - Secondary elements
  Tertiary: Maroon (#722F37) - Titles, emphasis
  Outline: Deep Charcoal (#2D2D2D) - Boundaries and strokes

Visual Elements:
  - Isometric/2D technical illustrations
  - 3-5 explanatory text boxes per slide
  - Simplified Chinese callout labels
  - Faded thematic background patterns
  - Clean black outlines on all elements
  - Split or triptych layouts

Density Guidelines:
  - Content per slide: Dense information organized with clear visual hierarchy, 3-5 explanatory text boxes
  - Whitespace: Balanced but compact, using text boxes and frames to structure space

Style Rules:
  Do:
    - Include substantive content from source
    - Use Simplified Chinese callout labels
    - Retain subtle aged paper texture
    - Maintain clear visual hierarchy
  Don't:
    - Use photorealistic renders
    - Apply digital gradients or glossy effects
    - Include slide numbers, footers, or logos
</STYLE_INSTRUCTIONS>

---

## SLIDE CONTENT

## Slide 15 of 20

**Type**: Content
**Filename**: 15-slide-step4-hmi.png

// NARRATIVE GOAL
介绍步骤四的前端交互设计：HMI在发生安全警报或Checker接管时，如何向船员提供交互以进行应急处理。

// KEY CONTENT
Headline: [安全警报与接管交互设计]
Sub-headline: 安全检查面板设计：确保人机共驾的最终安全防线
Body:
- 异构状态显式指示: 用独立的UI色板高亮标识“Doer健康状态”与“Checker监控状态”。
- 强声光接管弹窗: Checker介入接管时，屏幕中央弹出红色卡片，显示“Checker已执行接管，进入安全兜底模式”。
- 故障信息透明: 详细列出导致接管的具体原因，例如“M5路径计算超时”或“来船侵入核心安全圈”。
- 一键紧急切回: 提供物理级显眼的人工一键接管/恢复正常航行交互按钮，保障人在回路(HITL)安全。

// VISUAL
A dramatic high-contrast alert dashboard mockup. The center of the screen is overlaid with a large red alarm popup box with a warning triangle icon, displaying the takeover log: "TIMEOUT: M5 PLANNER". A large black button reads "CONFIRM EMERGENCY HANDOVER".

// LAYOUT
Layout: split-screen
Left side: UI alerts and takeover layout description. Right side: Visual UI mockup showing the high-priority takeover dialog box.

---

Please use nano banana pro to generate the slide image based on the content provided above.
