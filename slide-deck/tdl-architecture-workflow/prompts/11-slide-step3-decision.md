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

## Slide 11 of 20

**Type**: Content
**Filename**: 11-slide-step3-decision.png

// NARRATIVE GOAL
展示避碰流程第三步：避碰决策生成。介绍M4和M5如何生成具体的最优控制指令与避碰路径。

// KEY CONTENT
Headline: [步骤三：避碰决策生成]
Sub-headline: M4行为仲裁器与M5战术规划器的协同求解
Body:
- M4 IvP行为仲裁: M4 利用区间规划(IvP)算法，在航线跟踪、避碰合规和操纵性之间寻找Pareto最优解。
- 约束投影化: M6的COLREGs约束集作为刚性惩罚项，限制M4的目标函数求解空间。
- M5 规划器求解: M5 采用BC-MPC（基于动力学模型的预测控制）对规划的航向进行曲线平滑。
- 控制矢量输出: 输出我船目标航向(ψ)、目标船速(u)和回转率(ROT)命令，并携带 stamp 与 rationale。

// VISUAL
A 2D curve plot showing multiple trajectory candidates (wavy lines) diverging from a ship's current position to bypass an obstacle vessel. The chosen trajectory is highlighted with a solid dark line, while others are gray and dashed. Annotations of target parameters.

// LAYOUT
Layout: split-screen
Left side: Mathematical and logical synergy between IvP arbitration and MPC path planning. Right side: Trajectory planning grid showing how Candidates are generated and pruned.

---

Please use nano banana pro to generate the slide image based on the content provided above.
