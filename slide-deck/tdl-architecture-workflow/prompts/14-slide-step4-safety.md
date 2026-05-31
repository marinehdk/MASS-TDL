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

## Slide 14 of 20

**Type**: Content
**Filename**: 14-slide-step4-safety.png

// NARRATIVE GOAL
展示避碰流程第四步：安全监督与兜底。介绍M7如何作为独立的Checker对系统进行实时验证。

// KEY CONTENT
Headline: [步骤四：安全监督与兜底]
Sub-headline: M7 Safety Supervisor 的 Checker 角色与独立防碰撞机制
Body:
- 独立于Doer链路: M7 拥有独立的ROS包、独立的生命周期管理以及独立的运行路径，逻辑极简化。
- 关键假设监控: 实时监控系统关键假设（如“来船AIS数据可信”、“我船舵角响应正常”）。
- SOTIF/功能安全防线: 依据ISO 21448与IEC 61508，检测Doer是否出现超越包络、算法停转或不可行路径。
- Veto接管权限: 当评估防碰撞时延逼近红线且Doer无响应时，M7直接触发接管并执行最小风险操作(MRC)。

// VISUAL
A large circular lock shield icon with a technical checkmark inside. Connecting lines flow from sensors directly to the shield, then bypass the main controller to go directly to the rudder actuator schema, indicating the veto takeover channel.

// LAYOUT
Layout: split-screen
Left side: Logic rules and safety standard compliance of the Checker. Right side: Takeover logic flowchart illustrating the bypass control scheme.

---

Please use nano banana pro to generate the slide image based on the content provided above.
