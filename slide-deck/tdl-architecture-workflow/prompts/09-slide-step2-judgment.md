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

## Slide 9 of 20

**Type**: Content
**Filename**: 09-slide-step2-judgment.png

// NARRATIVE GOAL
展示避碰流程第二步：综合态势研判。后端算法如何通过COLREGs推理机和包络管理器实现逻辑研判。

// KEY CONTENT
Headline: [步骤二：综合态势研判]
Sub-headline: M1包络控制与M6规则推理机责任与态势判定
Body:
- 避碰责任划分: M6 依据COLREGs第12-17条判断我船是让路船(Give-way)还是直航船(Stand-on)。
- 条款规则激活: 激活具体责任条款，如“Rule 15 交叉相遇情景下，让路船应及早采取宽让行动”。
- ODD包络评估: M1评估风浪流等外部环境扰动，判断我船操控包络线是否具备安全规避的机动能力。
- 约束集输出: M6将避碰方向（如“仅限右转规避”）及避碰时机打包为 `/l3/m6/colregs_constraint`。

// VISUAL
A schematic of COLREGs rule classification. Three circular sectors radiating from a center ship represent Overtaking (135°), Crossing (112.5°), and Head-On (22.5°). Arrows indicate the allowed maneuver directions (mostly to starboard).

// LAYOUT
Layout: split-screen
Left side: Logic description of Rule Reasoner and ODD state evaluation. Right side: Technical drawing of the nautical collision avoidance sectors with rule numbers.

---

Please use nano banana pro to generate the slide image based on the content provided above.
